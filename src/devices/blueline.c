/** @file
    Blueline PowerCost Monitor protocol.

    Copyright (C) 2020 Justin Brzozoski

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include <stdlib.h>
#include "fatal.h"
#include "decoder.h"

/**
BlueLine Innovations Power Cost Monitor, tested with BLI-28000.

Much of the groundwork for this implementation was based on reading the source and notes from older
implementations, but this implementation was a fresh rewrite by Justin Brzozoski in 2020.  I would
not have been able to figure this out without the other implementations to look at, but I wanted an
implementation that didn't need to know the Kh factor or monitor ID ahead of time, which required
changes.

Some references used include:

https://github.com/merbanan/rtl_433/pull/38 - an abandoned pull request on rtl_433 by radredgreen

https://github.com/CapnBry/Powermon433 - a standalone Arduino-based Blueline monitor

http://scruss.com/blog/2013/12/03/blueline-black-decker-power-monitor-rf-packets/ - the blog post
where the other authors were trading notes in the comments

The IR-reader/sensor will transmit 3 bursts every ~30 seconds.  The low-level encoding is on/off
keyed pulse-position modulation (OOK_PPM).  The on pulses are always 0.5ms, while the off pulses
are either 0.5ms for logic 1 or 1.0ms for logic 0.  Each burst is 32 bits long.  The pauses
between the 3 grouped bursts is roughly 100ms.

Data is sent less significant byte first for multi-byte fields.

The basic layout of all bursts is as follows:

- First is a 1 byte header, which is always the value 0xFE.
- Second is a 2 byte payload, which is interpreted differently based on the two lowest bits of the first byte
- Finally is a 1 byte CRC, calculated across the 2 payload bytes (not the header)

The CRC is a CRC-8-ATM with polynomial 100000111, but it may be required to modify the payload bytes before
calculating it depending on message type.

There are 4 message types that can be indicated by the 2 lowest bits of the first payload byte:
- 0: ID message (payload is not offset)
- 1: power message (payload is offset)
- 2: temperature/status message (payload is offset)
- 3: energy message (payload is offset)

For the ID message (0), the CRC can be calculated directly on the payload as sent, and when the payload is
interpreted as a 16-bit integer it gives the ID of the transmitter.  This message is sent when the
monitor is first powered on and if the button on the monitor is pressed briefly.  If the button on the
monitor is held for >10 seconds, the monitor will change it's ID and report the new one.

While the transmitter ID's are 16-bit, none of them can have any of the two lowest bits set or they
would not be able to transmit their ID as message type 0, and when the offset is calculated (see below) they
would also change the message type.

For the 3 other message types, the payload must be offset before calculating the CRC or
interpreting the data.  The offset is done by treating the whole payload as a single 16-bit integer and
then subtracting the ID of the transmitter.  After the offset is done, then the CRC may be calculated
and the payload may be interpreted.

Note that if the transmitter's ID isn't known, the code can't easily determine if messages other than an
ID payload are good or bad, and can't interpret their data correctly.  However, if the "auto" mode is enabled,
the system can try to learn the transmitter's ID by various methods. (See USAGE HINTS below)

For the power message (1), the offset payload gives the number of milliseconds gap between impulses for the most
recent impulses seen by the monitor.  To convert from this 'gap' to kilowatts, you will need your meter's
Kh value. The Kh value is written obviously on the front of most meters, and 1.0 and 7.2 are very common.

    kW = (3600/gap) * Kh

Note that the 'gap' value clamps to a maximum of 65533 (0xFFFD), so there is a non-zero floor when calculating the
kW value using this report.  For example, with a Kh of 7.2, the lowest kW value you will ever see when monitoring
the 'gap' value is (3600/65533)*7.2 = 0.395kW.  If you need power monitoring for impulse rates slower than every
65.533 seconds to do things like confirm that your power consumption is 0kW, you need to monitor the impulse
counts and timing between energy messages (see 3 below).

For the temperature message (2), the offset payload gives the temperature in an odd scaling in the last byte,
and has some flag bits in the first byte.  The only known flag bit is the battery.  rtl_433 handles scaling back to
degrees celsius automatically.

For the energy message (3), the offset payload contains a continuously running power impulse accumulator. I'm
not sure if there is a way to reset the accumulator.  The intended way to use it is to remember the accumulator
value at the beginning of a time period, and then subtract that from the value at the end of the time period.  The
accumulator will roll over to 0 after 65535.

    kWh = 0.001 * (accumulated pulses) * Kh

Since the Kh value on all meters can vary, we do not handle it in rtl_433 and just report the raw millisecond
gap and accumulated impulses as received directly from the monitor.

## Usage hints:

The requirement of knowing the ID before being able to receive a message means that this decoder
will generally require a parameter to be useful.  When running in the default mode with no parameters,
the only message it is able to decode is the one that announces a monitor's ID.  So, assuming you can get to
the monitor to power cycle it or hit the button, this is the recommended method:

- 1) Start rtl_433
- 2) Tap the button or power cycle the monitor
- 3) Look for the rtl_433 output indicating the BlueLine monitor ID and note the ID field
- 4) Stop rtl_433
- 5) Restart rtl_433, explicitly passing the ID as a parameter to this decoder

For example, if you see the ID 45364 in step 3, you would start the decoder with a command like:

    rtl_433 -R 176:45364

If you are unable to access the monitor to have it send the ID message, you can also use the "auto" parameter:

    rtl_433 -vv -R 176:auto

Verbose mode should be specified first on the command line to see what the "auto" mode is doing.

The auto parameter will try to brute-force the ID on any messages that look like they are from a
BlueLine monitor.  This method usually succeeds within a few minutes, but is likely to get false positives
if there is more than one monitor in range or the messages being received are all identical (i.e. if the
meter is continuously reporting 0 watts).  If it succeeds, it will start reporting data with the new ID,
which you should then use as a parameter when you re-run rtl_433 in the future.

Finally, passing a parameter to this decoder requires specifying it explicitly, which normally disables all
other default decoders.  If you want to pass an option to this decoder without disabling all the other defaults,
the simplest method is to explicitly exclude this one decoder (which implicitly says to leave all other defaults
enabled), then add this decoder back with a parameter.  The command line looks like this:

    rtl_433 -R -176 -R 176:45364

*/

#define BLUELINE_BITLEN      32
#define BLUELINE_STARTBYTE   0xFE
#define BLUELINE_CRC_POLY    0x07
#define BLUELINE_CRC_INIT    0x00
#define BLUELINE_CRC_BYTELEN 2
#define BLUELINE_TXID_MSG        0x00
#define BLUELINE_POWER_MSG       0x01
#define BLUELINE_TEMPERATURE_MSG 0x02
#define BLUELINE_ENERGY_MSG      0x03

#define BLUELINE_ID_STEP_SIZE 4
#define MAX_POSSIBLE_BLUELINE_IDS (65536/BLUELINE_ID_STEP_SIZE)
#define BLUELINE_ID_GUESS_THRESHOLD 4

struct blueline_stateful_context {
    unsigned id_guess_hits[MAX_POSSIBLE_BLUELINE_IDS];
    uint16_t current_sensor_id;
    unsigned searching_for_new_id;
};

static uint8_t rev_crc8(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t remainder)
{
    unsigned byte, bit;

    // Run a CRC backwards to find out what the init value would have been.
    // Alternatively, put a known init value in the first byte, and it will
    // return a value that could be used in that place to get that init.

    // This logic only works assuming the polynomial has the lowest bit set,
    // Which should be true for most CRC polynomials, but let's be safe...
    if ((polynomial & 0x01) == 0) {
        fprintf(stderr, "Cannot run reverse CRC-8 with this polynomial!\n");
        return 0xFF;
    }
    polynomial = (polynomial >> 1) | 0x80;

    byte = nBytes;
    while (byte--) {
        bit = 8;
        while (bit--) {
            if (remainder & 0x01) {
                remainder = (remainder >> 1) ^ polynomial;
            }
            else {
                remainder = remainder >> 1;
            }
        }
        remainder ^= message[byte];
    }
    return remainder;
}

static uint16_t guess_blueline_id(r_device *decoder, const uint8_t *current_row)
{
    struct blueline_stateful_context *const context = decoder->decode_ctx;
    const uint16_t start_value = ((current_row[2] << 8) | current_row[1]);
    const uint8_t recv_crc = current_row[3];
    const uint8_t rcv_msg_type = (current_row[1] & 0x03);
    uint16_t working_value;
    uint8_t working_buffer[2];
    uint8_t reverse_crc_result;
    uint16_t best_id;
    unsigned best_hits;
    unsigned num_at_best_hits;
    unsigned high_byte_steps;

    // TL;DR - Try all possible IDs against every incoming message, and count how many times each one
    // succeeds.  If one of them passes a threshold, assume it must be the right one and return it.

    // We do some optimizations to try and do all those checks quickly, but it's still about the
    // same as doing a CRC across 512 bytes of data for every 2 byte payload received.

    working_buffer[0] = BLUELINE_CRC_INIT;
    working_buffer[1] = current_row[2];
    high_byte_steps = 256;
    best_id = 0;
    best_hits = 0;
    num_at_best_hits = 0;
    while (high_byte_steps--) {
        reverse_crc_result = rev_crc8(working_buffer, BLUELINE_CRC_BYTELEN, BLUELINE_CRC_POLY, recv_crc);
        // Would this byte value have been usable while still being the same type of message we received?
        if ((reverse_crc_result & 0x03) == rcv_msg_type) {
            working_value = ((working_buffer[1] << 8) | reverse_crc_result);
            working_value = start_value - working_value;
            context->id_guess_hits[(working_value/BLUELINE_ID_STEP_SIZE)]++;
            if (context->id_guess_hits[(working_value/BLUELINE_ID_STEP_SIZE)] >= best_hits) {
                if (context->id_guess_hits[(working_value/BLUELINE_ID_STEP_SIZE)] > best_hits) {
                    best_hits = context->id_guess_hits[(working_value / BLUELINE_ID_STEP_SIZE)];
                    best_id = working_value;
                    num_at_best_hits = 1;
                } else {
                    num_at_best_hits++;
                }
            }
        }
        working_buffer[1] += 1;
    }

    decoder_logf(decoder, 1, __func__, "Attempting Blueline autodetect: best_hits=%u num_at_best_hits=%u", best_hits, num_at_best_hits);
    return ((best_hits >= BLUELINE_ID_GUESS_THRESHOLD) && (num_at_best_hits == 1)) ? best_id : 0;
}

static int blueline_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    struct blueline_stateful_context *const context = decoder->decode_ctx;
    data_t *data;
    int row_index;
    uint8_t *current_row;
    int payloads_decoded = 0;
    int most_applicable_failure = 0;
    uint8_t calc_crc;
    uint16_t offset_payload_u16 = 0;
    uint8_t offset_payload_u8[BLUELINE_CRC_BYTELEN] = {0};

    // Blueline uses inverted 0/1
    bitbuffer_invert(bitbuffer);

    // Look at each row we just received independently
    for (row_index = 0; row_index < bitbuffer->num_rows; row_index++) {
        current_row = bitbuffer->bb[row_index];

        // All valid rows will have a fixed length and start with the same byte
        if ((bitbuffer->bits_per_row[row_index] != BLUELINE_BITLEN) || (current_row[0] != BLUELINE_STARTBYTE)) {
            if (DECODE_ABORT_LENGTH < most_applicable_failure) {
                most_applicable_failure = DECODE_ABORT_LENGTH;
            }
            continue;
        }

        // We need to know which type of message to decide how to check CRC
        const unsigned message_type = (current_row[1] & 0x03);
        const uint8_t recv_crc = current_row[3];

        if (message_type == BLUELINE_TXID_MSG) {
            // No offset required before CRC or data handling
            calc_crc = crc8(&current_row[1], BLUELINE_CRC_BYTELEN, BLUELINE_CRC_POLY, BLUELINE_CRC_INIT);
        } else {
            // Offset required before CRC or datahandling
            offset_payload_u16 = ((current_row[2] << 8) | current_row[1]) - context->current_sensor_id;
            offset_payload_u8[0] = (offset_payload_u16 & 0xFF);
            offset_payload_u8[1] = (offset_payload_u16 >> 8);
            calc_crc = crc8(&offset_payload_u8[0], BLUELINE_CRC_BYTELEN, BLUELINE_CRC_POLY, BLUELINE_CRC_INIT);
        }

        // If the CRC didn't match up, ignore this row!
        if (calc_crc != recv_crc) {
            if ((context->searching_for_new_id) && (message_type != BLUELINE_TXID_MSG)) {
                uint16_t id_guess = guess_blueline_id(decoder, current_row);
                if (id_guess != 0) {
                    decoder_logf(decoder, 1, __func__,"Switching to auto-detected Blueline ID %u", id_guess);
                    context->current_sensor_id = id_guess;
                    context->searching_for_new_id = 0;
                }
            }
            if (DECODE_FAIL_MIC < most_applicable_failure) {
                most_applicable_failure = DECODE_FAIL_MIC;
            }
            continue;
        }

        if (message_type == BLUELINE_TXID_MSG) {
            const uint16_t received_sensor_id = ((current_row[2] << 8) | current_row[1]);
            /* clang-format off */
            data = data_make(
                    "model",        "",             DATA_STRING, "Blueline-PowerCost",
                    "id",           "",             DATA_INT,    received_sensor_id,
                    "mic",          "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            payloads_decoded++;
            if (context->searching_for_new_id) {
                decoder_logf(decoder, 1, __func__,"Switching to received Blueline ID %u", received_sensor_id);
                context->current_sensor_id = received_sensor_id;
                context->searching_for_new_id = 0;
            }
        } else if (message_type == BLUELINE_POWER_MSG) {
            const uint16_t ms_per_pulse = offset_payload_u16;
            /* clang-format off */
            data = data_make(
                    "model",        "",             DATA_STRING, "Blueline-PowerCost",
                    "id",           "",             DATA_INT,    context->current_sensor_id,
                    "gap",          "",             DATA_INT,    ms_per_pulse,
                    "mic",          "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            payloads_decoded++;
        } else if (message_type == BLUELINE_TEMPERATURE_MSG) {
            // TODO - Confirm battery flag is working properly

            // These were the estimates from Powermon433.
            // But they didn't line up perfectly with my LCD display.
            //
            // A: deg_f = 0.823 * recvd_temp - 28.63
            // B: deg_c = 0.457 * recvd_temp - 33.68

            // I logged raw radio values and their resulting display temperatures
            // for a range of -13 to 34 degrees C, and it's not perfectly linear.
            // It's not so far off that I think we should use something other than
            // a linear fit, but it's likely got some fixed-point truncation errors
            // in the official display code.
            //
            // I put all the points I had into Excel and asked for the best
            // linear estimate, and it gave me roughly this:
            //
            // deg_C = 0.436 * recvd_temp - 30.36

            // In case anyone else wants to continue try and find a better equation,
            // a full copy of my logged data is in the comments of this GitHub pull
            // request:
            //
            // https://github.com/merbanan/rtl_433/pull/1590

            const uint8_t temperature = offset_payload_u8[1];
            const uint8_t flags = offset_payload_u8[0] >> 2;
            const uint8_t battery = (flags & 0x20) >> 5;
            const float temperature_C = (0.436 * temperature) - 30.36;
            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, "Blueline-PowerCost",
                    "id",               "",             DATA_INT,    context->current_sensor_id,
                    "flags",            "",             DATA_FORMAT, "%02x", DATA_INT, flags,
                    "battery_ok",       "Battery",      DATA_INT,    !battery,
                    "temperature_C",    "",             DATA_DOUBLE, temperature_C,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            payloads_decoded++;
        } else { // Assume BLUELINE_ENERGY_MSG
            // (The lowest two bits of the pulse count will always be the same because message_type is overlaid there)
            const uint16_t pulses = offset_payload_u16;
            /* clang-format off */
            data = data_make(
                    "model",            "",             DATA_STRING, "Blueline-PowerCost",
                    "id",               "",             DATA_INT, context->current_sensor_id,
                    "impulses",         "",             DATA_INT,    pulses,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            payloads_decoded++;
        }
    }

    return ((payloads_decoded > 0) ? payloads_decoded : most_applicable_failure);
}

static char const *const output_fields[] = {
        "model",
        "id",
        "flags",
        "gap",
        "impulses",
        "battery_ok",
        "temperature_C",
        "mic",
        NULL,
};

r_device const blueline;

static r_device *blueline_create(char *arg)
{
    r_device *r_dev = create_device(&blueline);
    if (!r_dev) {
        fprintf(stderr, "blueline_create() failed\n");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    struct blueline_stateful_context *context = malloc(sizeof(*context));
    if (!context) {
        WARN_MALLOC("blueline_create()");
        free(r_dev);
        return NULL; // NOTE: returns NULL on alloc failure.
    }
    memset(context, 0, sizeof(*context));
    r_dev->decode_ctx = context;

    if (arg != NULL) {
        if (strcmp(arg, "auto") == 0) {
            // Setup for auto identification
            context->searching_for_new_id = 1;
            //fprintf(stderr, "Blueline decoder will try to autodetect ID.\n");
        } else {
            // Assume user is trying to pass in hex ID
            context->current_sensor_id = strtoul(arg, NULL, 0);
            //fprintf(stderr, "Blueline decoder using ID %u\n", context->current_sensor_id);
        }
    }

    return r_dev;
}

r_device const blueline = {
        .name        = "BlueLine Innovations Power Cost Monitor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 500,
        .long_width  = 1000,
        .gap_limit   = 2000,
        .reset_limit = 8000,
        .decode_fn   = &blueline_decode,
        .create_fn   = &blueline_create,
        .fields      = output_fields,
};
