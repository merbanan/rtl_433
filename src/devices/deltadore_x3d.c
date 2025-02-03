/** @file
    Decoder for DeltaDore X3D devices.

    Copyright (C) 2021 Sven Fabricius <sven.fabricius@livediesel.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int deltadore_x3d_decode(r_device *decoder, bitbuffer_t *bitbuffer)
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
- Header            {n}
- Msg Payload       {n}
- CRC16             {16}

Known Data:
- Length            {8}
- Unknown           {8}  always 0xff
- Msg No.           {8}
- Msg Type          {8}
   - 0x00   sensor message (from window detector)
   - 0x01   standard message
   - 0x02   setup message
   - 0x03   actor identification beacon (click/clack)
- Header Len/Flag   {8}
   - possible upper 3 bits are flags, if bit 5 (0x20) is set, then no message payload is attached
   - lower 5 bits align with length of following header
- Device ID         {32}  always the device ID of the thermostat, assigned switch actors send same id.
- Unknown           {8}
- Some Flags        {8}
   - Msg type 0: 0x41 window was opened, 0x01 window was closed, 0x00 nothing changed
- Some Flags        {8}
   - 0x00           nothing followed
   - 0x01
     - unknown      {8} 0x00
   - 0x08 then following temperature
     - unknown      {8} 0x00
     - temperature  {16} int little-endian multiplied by 100 -> 2050 = 20.5 °C
- Msg Id            {16} some random value
- Header Chk        {16} big-endian negated cross sum of the header part from device id on

Optional message payload:
The payload extends by the count of connected actors and also the retry count and actor number
- Retry Cnt         {8} used for msg retry ? and direction
   - lower nibble and zero send from thermostat, count downwards
   - upper nibble send from switch actor, count upwards
- Act No.           {8} target actor number

Payload standard message 0x01:
- Unknown           {8}  always 0x00
- Response          {8}  0x00 from Thermostat, 0x01 answer from actor
- Unknown           {16}  0x0001 or 0x0000
- Command?          {16}
   - 0x0001        read register
   - 0x0008        status?
   - 0x0009        write register
- Register No       {16}
- Command resp      {8}  0x01 successful
- Unknown           {8}  0x00
- 1st Value         {8}
- 2nd Value         {8}

Register address:
- register area  {8}  x11, x15, x16, x18, x19, x1a
- register no.   {8}

  - 11-51: Unknown
  - 15-21: Unknown

  - 16-11: Current Target Temp and status
    - Get current Target Temp  {8}
    - Status                   {8}
      0x10  Heater on
      0x20  unknown
      0x80  Window open

  - 16-31:
    - Set current Target Temp  {8}
    - Enabled Modes?           {8}
      Could be a Bitmask or enum:
      00 = manual
      02 = Freeze mode
      07 = Auto mode
      08 = Holiday/Party mode

  - 16-41: on off state?
    - 3907 = on, 3807 = off

  - 16-61: {16}
    - Party on time in minutes
    - Holiday time in minutes starting from current time.
      (Days - 1) * 1440 + Current Time in Minutes

  - 16-81:
    - Freeze Temp {8}
    - unknown     {8}

  - 16-91:
    - Night Temp  {8}
    - Day Temp    {8}

  - 18-01: Unknown

  - 19-10 (RO): {16} On time lsb in seconds
  - 19-90 (RO): {16} On time msb
    used to calculate energy consumption

  - 1a-04: Unknown

  The switch temperature is calculated in 0.5 °C steps.

The length including payload is whitened using CCITT whitening enabled in SX1211 chipset.
The payload contains some garbage at the end. The documentation of the SX1211 assume to
exclude the length byte from length calculation, but the CRC16 checksum at the end is so placed,
that the length byte is included. Maybe someone read the docs wrong. The garbage after the
checksum contains data from previous larger messages.

So the last two bytes contains the CRC16(Poly=0x1021,Init=0x0000) value.

To get raw data:

    ./rtl_433 -f 868.95M -X 'n=DeltaDore,m=FSK_PCM,s=25,l=25,r=800,preamble=aa8169967e'
*/

// ** DeltaDore X3D known message types
#define DELTADORE_X3D_MSGTYPE_SENSOR          0x00
#define DELTADORE_X3D_MSGTYPE_STANDARD        0x01
#define DELTADORE_X3D_MSGTYPE_PAIRING         0x02
#define DELTADORE_X3D_MSGTYPE_BEACON          0x03

#define DELTADORE_X3D_HEADER_LENGTH_MASK      0x1f
#define DELTADORE_X3D_HEADER_FLAGS_MASK       0xe0
#define DELTADORE_X3D_HEADER_FLAG_NO_PAYLOAD  0x20
#define DELTADORE_X3D_HEADER_FLAG3_EMPTY_BYTE 0x01
#define DELTADORE_X3D_HEADER_FLAG3_TEMP       0x08
#define DELTADORE_X3D_HEADER_FLAG2_WND_CLOSED 0x01
#define DELTADORE_X3D_HEADER_FLAG2_WND_OPENED 0x41
#define DELTADORE_X3D_HEADER_TEMP_INDOOR      0x00
#define DELTADORE_X3D_HEADER_TEMP_OUTDOOR     0x01

#define DELTADORE_X3D_MAX_PKT_LEN             (64U)

struct deltadore_x3d_message_header {
    uint8_t number;
    uint8_t type;
    uint8_t header_len;
    uint8_t header_flags;
    uint32_t device_id;
    uint8_t network;
    uint8_t unknown_header_flags1;
    uint8_t unknown_header_flags2;
    uint8_t unknown_header_flags3;
    uint8_t temp_type;
    int16_t temperature;
    uint16_t message_id;
    int16_t header_check;
};

struct deltadore_x3d_message_payload {
    uint8_t retry;
    uint16_t transfer;
    uint16_t transfer_ack;
    uint16_t target;
    uint8_t action;
    uint8_t register_high;
    uint8_t register_low;
    uint16_t target_ack;
};

/* clang-format off */
static uint32_t deltadore_x3d_read_le_u24(uint8_t **buffer)
{
    uint32_t res = **buffer; (*buffer)++;
    res |= **buffer << 8;    (*buffer)++;
    res |= **buffer << 16;   (*buffer)++;
    return res;
}

static uint16_t deltadore_x3d_read_le_u16(uint8_t **buffer)
{
    uint16_t res = **buffer; (*buffer)++;
    res |= **buffer << 8;    (*buffer)++;
    return res;
}

static uint16_t deltadore_x3d_read_be_u16(uint8_t **buffer)
{
    uint16_t res = **buffer << 8; (*buffer)++;
    res |= **buffer;              (*buffer)++;
    return res;
}
/* clang-format on */

static uint8_t deltadore_x3d_parse_message_header(uint8_t *buffer, struct deltadore_x3d_message_header *out)
{
    uint8_t bytes_read         = 14;
    out->number                = *buffer++;
    out->type                  = *buffer++;
    out->header_len            = *buffer & DELTADORE_X3D_HEADER_LENGTH_MASK;
    out->header_flags          = *buffer++ & DELTADORE_X3D_HEADER_FLAGS_MASK;
    out->device_id             = deltadore_x3d_read_le_u24(&buffer);
    out->network               = *buffer++;
    out->unknown_header_flags1 = *buffer++;
    out->unknown_header_flags2 = *buffer++;
    out->unknown_header_flags3 = *buffer++;
    if (out->unknown_header_flags3 == DELTADORE_X3D_HEADER_FLAG3_EMPTY_BYTE) {
        buffer++;
        bytes_read++;
    }
    else if (out->unknown_header_flags3 == DELTADORE_X3D_HEADER_FLAG3_TEMP) {
        out->temp_type   = *buffer++;
        out->temperature = deltadore_x3d_read_le_u16(&buffer);
        bytes_read += 3;
    }
    out->message_id   = deltadore_x3d_read_le_u16(&buffer);
    out->header_check = deltadore_x3d_read_be_u16(&buffer);

    return bytes_read;
}

static uint8_t deltadore_x3d_parse_message_payload(uint8_t *buffer, struct deltadore_x3d_message_payload *out)
{
    out->retry         = *buffer++;
    out->transfer      = deltadore_x3d_read_le_u16(&buffer);
    out->transfer_ack  = deltadore_x3d_read_le_u16(&buffer);
    out->target        = deltadore_x3d_read_le_u16(&buffer);
    out->action        = *buffer++;
    out->register_high = *buffer++;
    out->register_low  = *buffer++;
    out->target_ack    = deltadore_x3d_read_le_u16(&buffer);
    return 12;
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
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, sizeof(preamble) * 8);

    if (start_pos >= bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY; // no preamble detected
    }

    // start after preamble
    start_pos += sizeof(preamble) * 8;

    // check min length
    if (bitbuffer->bits_per_row[row] < 10 * 8) { // preamble(4) + sync(4) + len(1) + data(1)
        return DECODE_ABORT_LENGTH;
    }

    // read length byte in advance
    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, row, start_pos, &len, 8);

    // dewhite length
    ccitt_whitening(&len, 1);

    if (len > DELTADORE_X3D_MAX_PKT_LEN) {
        decoder_logf(decoder, 1, __func__, "packet too large (%u bytes), dropping it\n", len);
        return DECODE_ABORT_LENGTH;
    }

    uint8_t frame[65] = {0};
    // Get whole frame (len includes the length byte)
    bitbuffer_extract_bytes(bitbuffer, row, start_pos, frame, len * 8);

    // dewhite the data
    ccitt_whitening(frame, len);

    decoder_log_bitrow(decoder, 2, __func__, frame, len * 8, "frame data");

    const uint16_t crc        = crc16(frame, len - 2, 0x1021, 0x0000);
    const uint16_t actual_crc = (frame[len - 2] << 8 | frame[len - 1]);

    if (actual_crc != crc) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x\n", actual_crc, crc);
        return DECODE_FAIL_MIC;
    }

    struct deltadore_x3d_message_header head = {0};
    uint8_t bytes_read                       = 2; // step over length and FF field
    bytes_read += deltadore_x3d_parse_message_header(&frame[bytes_read], &head);
    const char *class, *wnd_stat, *temp_type;

    /* clang-format off */
    switch (head.type) {
        case DELTADORE_X3D_MSGTYPE_SENSOR:   class = "Sensor";   break;
        case DELTADORE_X3D_MSGTYPE_STANDARD: class = "Standard"; break;
        case DELTADORE_X3D_MSGTYPE_PAIRING:  class = "Pairing";  break;
        case DELTADORE_X3D_MSGTYPE_BEACON:   class = "Beacon";   break;
        default:                             class = "Unknown";  break;
    }

    switch (head.unknown_header_flags2) {
        case DELTADORE_X3D_HEADER_FLAG2_WND_CLOSED: wnd_stat = "Closed"; break;
        case DELTADORE_X3D_HEADER_FLAG2_WND_OPENED: wnd_stat = "Opened"; break;
        default:                                    wnd_stat = "";       break;
    }

    switch (head.temp_type) {
        case DELTADORE_X3D_HEADER_TEMP_INDOOR:  temp_type = "indoor";  break;
        case DELTADORE_X3D_HEADER_TEMP_OUTDOOR: temp_type = "outdoor"; break;
        default:                                temp_type = "";        break;
    }
    /* clang-format on */


    /* clang-format off */
    data = data_make(
            "model",   "",            DATA_STRING, "DeltaDore-X3D",
            "id",      "",            DATA_INT,    head.device_id,
            "network", "Net",         DATA_INT,    head.network,
            "subtype", "Class",       DATA_FORMAT, "%s", DATA_STRING, class,
            "msg_id",  "Message Id",  DATA_INT,    head.message_id,
            "msg_no",  "Message No.", DATA_INT,    head.number,
            "mic",     "Integrity",   DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    // message from thermostate
    if (head.unknown_header_flags3 == DELTADORE_X3D_HEADER_FLAG3_TEMP) {
        float temperature = head.temperature / 100.0f;
        /* clang-format off */
        data = data_dbl(data, "temperature_C",    "Temperature", "%.1f", temperature);
        data = data_str(data, "temperature_type", "Temp Type",   NULL,   temp_type);
        /* clang-format on */
    }

    if (head.header_flags & DELTADORE_X3D_HEADER_FLAG_NO_PAYLOAD) {
        // Window stat from window sensor
        if (strlen(wnd_stat) > 0) {
            data = data_str(data, "wnd_stat", "Window Status", NULL, wnd_stat);
        }
    }
    else {
        struct deltadore_x3d_message_payload body = {0};
        bytes_read += deltadore_x3d_parse_message_payload(&frame[bytes_read], &body);

        // Max hex string len is 2 * (maximum packet length - crc) + NUL character
        char raw_str[2 * (DELTADORE_X3D_MAX_PKT_LEN - 2) + 1];

        /* clang-format off */
        data = data_int(data, "retry",         "Retry",             NULL, body.retry);
        data = data_int(data, "transfer",      "Transfer",          NULL, body.transfer);
        data = data_int(data, "transfer_ack",  "Transfer Ack",      NULL, body.transfer_ack);
        data = data_int(data, "target",        "Target",            NULL, body.target);
        data = data_int(data, "target_ack",    "Target Ack",        NULL, body.target_ack);
        data = data_int(data, "action",        "Action",            NULL, body.action);
        data = data_int(data, "register_high", "Reg High",          NULL, body.register_high);
        data = data_int(data, "register_low",  "Reg Low",           NULL, body.register_low);
        data = data_hex(data, "raw_msg",       "Raw Register Data", NULL, &frame[bytes_read], len - bytes_read - 2, raw_str);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);

    return 1;
}

static const char *const output_fields[] = {
        "model",
        "id",
        "network",
        "subtype",
        "msg_id",
        "msg_no",
        "temperature_C",
        "temperature_type",
        "wnd_stat",
        "retry",
        "transfer",
        "transfer_ack",
        "target",
        "action",
        "register_high",
        "register_low",
        "target_ack",
        "raw_msg",
        "mic",
        NULL,
};

r_device const deltadore_x3d = {
        .name        = "DeltaDore X3D devices",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 25,
        .long_width  = 25,
        .reset_limit = 800,
        .decode_fn   = &deltadore_x3d_decode,
        .fields      = output_fields,
};
