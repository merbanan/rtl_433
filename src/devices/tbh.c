/** @file
    Decoder for TBH Archos devices.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/**
Decoder for devices from the TBH project (https://www.projet-tbh.fr)

- Modulation: FSK PCM
- Frequency: 433.93MHz +-10kHz
- 212 us symbol/bit time

There exist several device types (power, meteo, gaz,...)

Payload format:
- Synchro           {32} 0xaaaaaaaa
- Preamble          {32} 0xd391d391
- Length            {8}
- Payload           {n}
- Checksum          {16} CRC16 poly=0x8005 init=0xffff

To get raw data:
./rtl_433 -f 433901000 -X n=tbh,m=FSK_PCM,s=212,l=212,r=217088 

The application data is obfuscated by doing data[n] xor data[n-1] xor info[n%16].

Payload foramt:
- Device id         {32}
- Frame type        {8}
- Frame Data        {x}

Frame types:
- Raw data      1
- Weather       2
- Battery level 3
- Battery low   4

Weather frame format:
- Type        {8} 02
- Temperature {16} unsigned in 0.1 Kelvin steps 
- Humidity    {16} unsigned % 

Raw data frame (power index):
- Version {8}
- Index     {24}
- Timestamp {34}
- MaxPower  {16}
- some additinal data ???
- CRC8 poly=0x7 the crc includes a length byte at the beginning
*/

#include "decoder.h"

static int
tbh_decode (r_device * decoder, bitbuffer_t * bitbuffer)
{
  uint8_t *b;
  data_t *data;
  int row;
  static uint8_t const preamble[] = {
    /*0xaa, 0xaa, */ 0xaa, 0xaa,  // preamble 
    0xd3, 0X91, 0xd3, 0x91        // sync word
  };

  if (!decoder || !bitbuffer) {
    return DECODE_ABORT_EARLY;
  }

  if (bitbuffer->num_rows != 1) {
    return DECODE_ABORT_EARLY;
  }

  for (row = 0; row < bitbuffer->num_rows; ++row) {
    // Validate message and reject it as fast as possible : check for preamble
    unsigned start_pos =
      bitbuffer_search (bitbuffer, row, 0, preamble, sizeof (preamble) * 8);
    if (decoder->verbose)
      fprintf (stderr, "start pos: %d\n", start_pos);

    if (start_pos == bitbuffer->bits_per_row[row])
      continue;    // no preamble detected, move to the next row

    // min length
    if (bitbuffer->bits_per_row[row] < 12 * 8) {  //sync(4) + preamble(4) + len(1) + data(1) + crc(2)
      return DECODE_ABORT_EARLY;
    }

    if (decoder->verbose)
      fprintf (stderr, "sync and preamble found\n");


    uint8_t len;
    bitbuffer_extract_bytes (bitbuffer, row,
			     start_pos + sizeof (preamble) * 8, &len, 8);

    if (decoder->verbose)
      fprintf (stderr, "got packet with %d bytes\n", len);


    if (len > 60) {
      if (decoder->verbose)
	fprintf (stderr, "packet to large (%d bytes), drop it\n", len);
      continue;
    }

    uint8_t frame[62] = { 0 };	//TODO check max size, I have no idea, arbitrary limit of 60 bytes + 2 bytes crc
    frame[0] = len;
    // Get frame (len don't include the length byte and the crc16 bytes)
    bitbuffer_extract_bytes (bitbuffer, row,
			     start_pos + (sizeof (preamble) + 1) * 8,
			     &frame[1], (len + 2) * 8);

    if (decoder->verbose) {
      fprintf (stderr, "frame data: ");
      for (int i = 0; i < len + 1; i++) {
	fprintf (stderr, " %02x", frame[i]);
      }
      fprintf (stderr, "\n");
    }

    uint16_t crc;
    crc = crc16 (frame, len + 1, 0x8005, 0xffff);

    if (decoder->verbose)
      fprintf (stderr, "got CRC %04x\n", crc);

    if ((frame[len + 1] << 8 | frame[len + 2]) != crc) {
      if (decoder->verbose) {
	fprintf (stderr, "CRC invalid %04x != %04x\n",
		 frame[len + 1] << 8 | frame[len + 2], crc);
      }
      continue;
    }
    else {
      if (decoder->verbose)
	fprintf (stderr, "CRC OK\n");
    }

    static uint8_t const info[] = {
      0x19, 0xF8, 0x28, 0x30, 0x6d, 0x0c, 0x94, 0x54,
      0x22, 0xf2, 0x37, 0xc9, 0x66, 0xa3, 0x97, 0x57
    };

    uint8_t payload[62] = { 0 };

    payload[0] = frame[1] ^ info[0];
    for (int i = 1; i < len; i++) {
      payload[i] = frame[i] ^ frame[i + 1] ^ info[i % sizeof (info)];
    }
    if (decoder->verbose) {
      fprintf (stderr, "payload: ");
      for (int i = 0; i < len; i++) {
	fprintf (stderr, " %02x", payload[i]);
      }
      fprintf (stderr, "\n");
    }

    uint8_t type = payload[4];
    uint32_t id =
      payload[0] | payload[1] << 8 | payload[2] << 16 | payload[3] << 24;

    switch (type) {
    case 1:
      // raw data
      if (decoder->verbose)
	fprintf (stderr, "raw data from ID: %08x\n", id);

      payload[4] = len - 4; //write len for crc (len - 4b ID)


      if (decoder->verbose) {
        fprintf (stderr, "data: ");
        for (int i = 4; i < len; i++) {
          fprintf (stderr, " %02x", payload[i]);
        }
        fprintf (stderr, "\n");
      }

      uint8_t c = crc8(&payload[4], len-5, 0x07, 0x00);


      if (c != payload[len-1]) {
          fprintf (stderr, "crc error\n");
	  continue;
      }

      uint32_t idx = payload[6] << 16 | payload[7] << 8 | payload[8];
      uint32_t ts = payload[9] << 16 | payload[10] << 8 | payload[11];
      uint32_t maxPower = payload[12] << 8 | payload[13];

      if (decoder->verbose)
        fprintf (stderr, "index: %d, timestamp: %d, maxPower: %d\n", idx, ts, maxPower);

      data = data_make ("model", "", DATA_STRING, "TBH",
			"id", "StationID", DATA_FORMAT, "%08X", DATA_INT, id,
			"power_idx", "Power index", DATA_FORMAT, "%d", DATA_INT, idx,
			"power_max", "Power max", DATA_FORMAT, "%d", DATA_INT, maxPower,
			"timestamp", "Timestamp", DATA_FORMAT, "%d s", DATA_INT, ts/8,
			NULL);
      decoder_output_data (decoder, data);
      break;
    case 2:
      {
	// temp and hym
	float temp = (payload[6] << 8 | payload[5]) - 2732;
	temp /= 10;
	int hym = payload[7];
	if (decoder->verbose)
	  fprintf (stderr, "ID: %08x info: %.1f°C %d%%\n", id, temp, hym);

	data = data_make ("model", "", DATA_STRING, "TBH weather",
			  "id", "StationID", DATA_FORMAT, "%08X", DATA_INT,
			  id, "temperature_C", "Temperature", DATA_FORMAT,
			  "%.01f °C", DATA_DOUBLE, temp,
			  "humidity", "Humidity", DATA_FORMAT, "%d %%", DATA_INT,
			  hym, NULL);
	decoder_output_data (decoder, data);
      }
      break;
    case 3:
      if (decoder->verbose)
	fprintf (stderr, "bat level received\n");
      // bat level, 0-100%
      data = data_make ("model", "", DATA_STRING, "TBH",
			"id", "StationID", DATA_FORMAT, "%08X", DATA_INT, id,
			"battery_level", "Battery level", DATA_FORMAT, "%d %%",
			DATA_INT, payload[5], NULL);
      decoder_output_data (decoder, data);
      break;
    case 4:
      if (decoder->verbose)
	fprintf (stderr, "bat low received\n");
      data = data_make ("model", "", DATA_STRING, "TBH",
			"id", "StationID", DATA_FORMAT, "%08X", DATA_INT, id,
			"battery", "", DATA_STRING, "LOW", NULL);
      decoder_output_data (decoder, data);
      // bat low
      break;
    default:
      if (decoder->verbose)
	fprintf (stderr, "unknown frame received\n");
      break;
    }

  }

  return 1;
}

static char *output_fields[] = {
  "model",
  "id",
  "temperature_C",
  "humidity",
  "battery",
  "battery_level",
  "power_idx",
  "power_max",
  "timestamp",
  NULL,
};

r_device tbh = {
  .name = "TBH weather sensor",
  .modulation = FSK_PULSE_PCM,
  .short_width = 212,
  .long_width = 212,
  .reset_limit = 217088,
  .decode_fn = &tbh_decode,
  .disabled = 0,
  .fields = output_fields,
};
