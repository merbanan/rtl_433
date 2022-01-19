/** @file
    Decoder for DeltaDore X3D devices.

    Copyright (C) 2021 Sven Fabricius <sven.fabricius@livediesel.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/**
Decoder for DeltaDore X3D devices.

Note: work in progress

- Modulation: FSK PCM
- Frequency: 868.95MHz
- 25 us bit time
- 40000 baud
- based on Semtech SX1211
- manual CRC

Payload format:
- Preamble          {32} 0xaaaaaaaa
- Syncword          {32} 0x8169967e
- Length            {8}
- Payload           {n}

Known Data:
- Length       {8}
- Unknown      {8}  always 0xff
- Msg No.      {8}
- Msg Type     {8}
   - 0x01        standard message
   - 0x02        setup message
   - 0x03        actor identifcation beacon (click/clack)
- Header Len   {8}
- Device ID   {16}  always the device ID of the thermostat, assigned switch actors send same id.
- Unknown     {32}  always 0x40000598
- Unknown      {8}  Influenced by header len
   - 0x00        on header len 0x0c
   - 0x08        on header len 0x0f
     - unknown      {8} 0x00  possible temp sign, if positive or negative
     - temperature {16} little-endian multiplied by 100 -> 2050 = 20.5 °C
- Msg Id      {32}
- Retry Cnt    {8} used for msg retry ? and direction
   - lower nibble and zero send from thermostat, count downwards
   - upper nibble send from switch actor, count upwards
- Act No.      {8} target actor number
payload data
- CRC16       {16}

Payload standard message 0x01:
- Unknown        {8}  always 0x00
- Response       {8}  0x00 from Thermostat, 0x01 answer from actor
- Unknown       {16}  0x0001 or 0x0000
- Command?      {16}
   - 0x0001        read register
   - 0x0008        status?
   - 0x0009        write register
- Register No   {16}
- Command resp  {8}  0x01 successful
- Unknown       {8}  0x00
- 1st Value     {8}
- 2nd Value     {8}
  
  Register 0x1631, 1st Value is thermal switch temperature in 0.5 °C steps.

Maybe all values are little-endian so register commands and so on are flipped.

The length including payload is whitened using CCITT whitening enabled in SX1211 chipset.
The payload contains some garbage at the end. The documentation of the SX1211 assume to
exclude the length byte from length calculation, but the CRC16 checksum at the end is so placed,
that the length byte is included. Maybe someone read the docs wrong. The garbage after the
checksum contains data from previous larger messages.

So the last two bytes contains the CRC16(Poly=0x1021,Init=0x0000) value.

To get raw data:

    ./rtl_433 -f 868.95M -X 'n=DeltaDore,m=FSK_PCM,s=25,l=25,r=800,preamble=aa8169967e'
*/

#include "decoder.h"

static void ccitt_dewhitening(uint8_t *whitening_key_msb_p, uint8_t *whitening_key_lsb_p, uint8_t *buffer, uint16_t buffer_size)
{
    uint8_t whitening_key_msb = *whitening_key_msb_p;
    uint8_t whitening_key_lsb = *whitening_key_lsb_p;
    uint8_t whitening_key_msb_previous;
    uint8_t reverted_whitening_key_lsb = whitening_key_lsb;

    for (uint16_t buffer_pos = 0; buffer_pos < buffer_size; buffer_pos++) {

        reverted_whitening_key_lsb = (whitening_key_lsb & 0xf0) >> 4 | (whitening_key_lsb & 0x0f) << 4;
        reverted_whitening_key_lsb = (reverted_whitening_key_lsb & 0xcc) >> 2 | (reverted_whitening_key_lsb & 0x33) << 2;
        reverted_whitening_key_lsb = (reverted_whitening_key_lsb & 0xaa) >> 1 | (reverted_whitening_key_lsb & 0x55) << 1;

        buffer[buffer_pos] ^= reverted_whitening_key_lsb;

        for (uint8_t rol_counter = 0; rol_counter < 8; rol_counter++) {
            whitening_key_msb_previous = whitening_key_msb;
            whitening_key_msb = (whitening_key_lsb & 0x01) ^ ((whitening_key_lsb >> 5) & 0x01);
            whitening_key_lsb = ((whitening_key_msb_previous << 7) & 0x80) | ((whitening_key_lsb >> 1) & 0xff);
        }
    }

    *whitening_key_msb_p = whitening_key_msb;
    *whitening_key_lsb_p = whitening_key_lsb;
}

static int deltadore_x3d_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {
            /*0xaa, 0xaa, */ 0xaa, 0xaa, // preamble
            0x81, 0x69, 0x96, 0x7e       // sync word
    };

    data_t *data;

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row = 0;
    // Validate message and reject it as fast as possible : check for preamble
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, sizeof (preamble) * 8);

    if (start_pos >= bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY; // no preamble detected
    }

    // start after preamble
    start_pos += sizeof (preamble) * 8;

    // check min length
    if (bitbuffer->bits_per_row[row] < 10 * 8) { //preamble(4) + sync(4) + len(1) + data(1)
        return DECODE_ABORT_LENGTH;
    }

    uint8_t whitening_key_msb = 0x01;
    uint8_t whitening_key_lsb = 0xff;

    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, row, start_pos, &len, 8);

    // dewhite length
    ccitt_dewhitening(&whitening_key_msb, &whitening_key_lsb, &len, 1);

    if (len > 64) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: packet to large (%d bytes), drop it\n", __func__, len);
        }
        return DECODE_ABORT_LENGTH;
    }

    uint8_t frame[65] = {0};
    frame[0] = len;

    // Get frame (len include the length byte)
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + 8, &frame[1], (len - 1) * 8);

    // dewhite data
    ccitt_dewhitening(&whitening_key_msb, &whitening_key_lsb, &frame[1], len - 1);

    if (decoder->verbose > 1) {
        bitrow_printf(frame, (len) * 8, "%s: frame data: ", __func__);
    }

    uint16_t crc = crc16(frame, len - 2, 0x1021, 0x0000);

    if ((frame[len - 2] << 8 | frame[len - 1]) != crc) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: CRC invalid %04x != %04x\n", __func__, frame[len - 2] << 8 | frame[len - 1], crc);
        }
        return DECODE_FAIL_MIC;
    }

    char frame_str[64 * 2 + 1] = {0};
    // do not put length and crc to raw data
    for (int i = 0; i < len - 3; ++i) {
        sprintf(&frame_str[i * 2], "%02x", frame[i + 1]);
    }

    /* clang-format off */
    data = data_make(
            "model",        "",                 DATA_STRING, "DeltaDore-X3D",
            "raw",          "Raw data",         DATA_STRING, frame_str,
            "mic",          "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "raw",
        "mic",
        NULL,
};

r_device deltadore_x3d = {
        .name        = "DeltaDore X3D devices",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 25,
        .long_width  = 25,
        .reset_limit = 800,
        .decode_fn   = &deltadore_x3d_decode,
        .fields      = output_fields,
};