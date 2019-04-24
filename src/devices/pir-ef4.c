/*
 * PIR-EF4SBT00003.
 * 
 * The sensor is known with the FCC-ID: EF4SBT00003
 *
 * Some references:
 * https://fcc.report/FCC-ID/EF4SBT00003
 * http://certid.org/fccid/EF4SBT00003
 * https://apps.fcc.gov/eas/GetEas731Report.do?applicationId=XkBhwjLuvh2pXcri3MP%2FWA%3D%3D&fcc_id=EF4SBT00003
 *
 *
 * The PIR-EF4 was produced by Nortek (nortekcontrol.com)
 * It was released in 1996 and works with 9 Volts battery.
 * The sensor broadcast its ID when it is triggered.
 * The ID is defined on 2 bytes.
 * There is no CRC or other data transmitted.

 * The modulation is OOK with PPM at the frequency of 315MHz. :

Guessing modulation: Pulse Position Modulation with fixed pulse width
Attempting demodulation... short_width: 848, long_width: 2132, reset_limit: 8488, sync_width: 0
Use a flex decoder with -X 'n=name,m=OOK_PPM,s=848,l=2132,g=2136,r=8488'

 *
 */

#include "decoder.h"

#define PIR_EF4_BITLEN        16
#define PIR_EF4_DATALEN       2


static int pir_ef4_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
  uint8_t packet[PIR_EF4_DATALEN];
  data_t *data;
  int id;

  // Correct number of rows?
  if (bitbuffer->num_rows != 1) {
    if (decoder->verbose > 1) {
      fprintf(stderr, "%s: wrong number of rows (%d)\n",
        __func__, bitbuffer->num_rows);
    }
    return 0;
  }

  // Correct bit length?
  if (bitbuffer->bits_per_row[0] != PIR_EF4_BITLEN) {
    if (decoder->verbose > 1) {
      fprintf(stderr, "%s: wrong number of bits (%d)\n",
        __func__, bitbuffer->bits_per_row[0]);
      }
    return 0;
  }

  // Print the ID:
  memcpy(packet, bitbuffer->bb[0]+1, PIR_EF4_DATALEN);
  id = (packet[0] << 8) | (packet[1]);

  // Pass the data:
  data = data_make(
                   "model",         "",            DATA_STRING, _X("PIR-EF4 sensor on 315MHz","PIR-EF4 sensor on 315MHz"),
                   "id",   "ID (16bit)",           DATA_FORMAT, "0x%x", DATA_INT, id,
                   NULL);

  decoder_output_data(decoder, data);

  return 1;
}

static char *pir_ef4_output_fields[] = {
    "model",
    "id",
    NULL
};

r_device pir_ef4 = {
    .name          = "PIR-EF4 sensor",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 848,
    .long_width    = 2116,
    .reset_limit   = 8488,
    .decode_fn     = &pir_ef4_callback,
    .disabled      = 0,
    .fields        = pir_ef4_output_fields,
};

