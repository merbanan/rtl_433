/*
* *** Honeywell (Ademco) Door/Window Sensors ***
*
* Tested with the Honeywell 5811 Wireless Door/Window transmitters
*
*/

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

uint16_t crc16_buypass(const uint8_t *data){
  uint16_t crc = 0xfffe;
  int i;
  size_t j;

  for(j=0; j<6; j++){
    crc ^= ((uint16_t) (data[j])) << 8;
    
    for(i=0; i<8; i++){
      if( (crc & 0x8000) ){
        crc <<= 1;
        crc ^= 0x8005;
      } else
        crc <<= 1;
    }
  }

  return (crc);
}

static int honeywell_callback(bitbuffer_t *bitbuffer) {
  if(bitbuffer->num_rows == 0)
    return 0;

  int good = 0;
  char time_str[LOCAL_TIME_BUFLEN];
  const uint8_t *bb;
  char device_id[6];
  char hex[2];
  char binary [65];
  int heartbeat = 0;
  uint16_t crc_calculated = 0;
  uint16_t crc = 0;

  local_time_str(0, time_str);

  if(bitbuffer->num_rows == 1 && bitbuffer->bits_per_row[0] == 64){
    good = 1;
    for(uint16_t i=0; i < 8; i++)
      bitbuffer->bb[0][i] = ~bitbuffer->bb[0][i];

    bb = bitbuffer->bb[0];
  }

  if(good){
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

    crc_calculated = crc16_buypass(bb);
    crc = (((uint16_t) bb[6]) << 8) + ((uint16_t) bb[7]);

    data_t *data = data_make(
                     "time",     "", DATA_STRING, time_str,
                     "id",       "", DATA_STRING, device_id,
                     "state",    "", DATA_STRING, ( (bb[5] & 0x80) == 0x00)? "closed":"open",
                     "heartbeat" , "", DATA_STRING, ( (bb[5] & 0x04) == 0x04)? "yes" : "no",
                     "checksum", "", DATA_STRING, crc == crc_calculated? "ok":"bad",
                     "time_unix","", DATA_INT, time(NULL),
                     "binary",   "", DATA_STRING, binary,
                            NULL);

    data_acquired_handler(data);
    return 1;
  }

  return 0; // Unrecognized data
}

static char *output_fields[] = {
        "time",
        "id",
        "state",
        "heartbeat",
        "checksum",
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
