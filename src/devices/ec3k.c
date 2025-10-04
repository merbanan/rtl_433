/** @file
    Decoder for Voltcraft Energy Count 3000 (ec3k, sold by Conrad) / Cost Control RT-110 , tested with:
      - Technoline Cost Control RT-110 (https://www.technotrade.de/produkt/technoline-cost-control-rt-110/), EAN 4029665006208

    Should also work with these devices from other manufacturers that use the same protocol or are even identical:
      - Voltcraft ENERGYCOUNT 3000 ENERGY LOGGER (Item No. 12 53 53, https://conrad-rus.ru/images/stories/virtuemart/media/125353-an-01-ml-TCRAFT_ENERGYC_3000_ENER_MESSG_de_en_nl.pdf)
        (don't mix that up with similar products from the same company like "Energy Check 3000", "Energy Monitor 3000" and "Energy Logger 4000")
      - Velleman (type NETBESEM4)
      - La Crosse Techology Remote Cost Control Monitor” (type RS3620)

    Copyright (C) 2025 Michael Dreher <michael(a)5dot1.de> @nospam2000

    decoding info taken from these projects:
      - https://github.com/EmbedME/ec3k_decoder (using rtl_fm)
      - https://github.com/avian2/ec3k (using python and gnuradio)
      
    Some more info can be found here:
      - https://www.sevenwatt.com/main/rfm69-energy-count-3000-elv-cost-control/
      - https://batilanblog.wordpress.com/2015/01/11/getting-data-from-voltcraft-energy-count-3000-on-your-computer/
      - https://web.archive.org/web/20121019130917/http://forum.jeelabs.net:80/comment/4020

    The device transmits every 5 seconds (if there is a change in power consumption) or every 30 minutes (if there is no change).
    It uses BFSK modulation with two frequencies between 30 and 80 kHz apart (e.g. 868.297 and 868.336 MHz).
    The bit rate is 20 kbit/s, so bit time is 50 us.

    The used chip is probably a AX5042 from On Semiconductor (formerly from Axsem), datasheet: https://www.onsemi.com/download/data-sheet/pdf/ax5042-d.pdf
    HDLC mode follows High−Level Data Link Control (HDLC, ISO 13239) protocol. HDLC Mode is the main framing mode of the AX5042.
    HDLC packets are delimited with flag sequences of content 0x7E. In AX5042 the meaning of address and control is user defined.
    The Frame Check Sequence (FCS) can be programmed to be CRC−CCITT, CRC−16 or CRC−32.
    The CRC is appended to the received data. There could be an optional flag byte after the CRC.

    The packet is NRZI encoded, with bit stuffing (a 0 is inserted after 5 consecutive 1 bits).
    The packet is framed by 0x7E (01111110) bytes at start and end.
    The packet length is 41 bytes (328 bits) excluding the two framing bytes.

    The following fields are decoded:
        id -- 16-bit ID of the device
        time_total -- time in seconds since last reset
        time_on -- time in seconds since last reset with non-zero device power
        energy -- total energy in Ws (watt-seconds)
        power_current -- current device power in watts
        power_max -- maximum device power in watts (reset at unknown intervals)
        reset_counter -- total number of transmitter resets
        device_on_flag -- true if device is currently drawing non-zero power
        crc

    The CRC is calculated over the packet excluding the leading and trailing framing byte 0x7E and the crc-value itself.
    The CRC bytes in the packet are in little-endian order (low byte first). I didn't find the parameters
    for a standard implementation, so I took the implementation from the python code at https://github.com/avian2/ec3k.

    Decoding works good with this params:
        rtl_433 -f 868300k -s 250k
        rtl_433 -f 868000k -s 1000k
        rtl_433 -f 868200k -s 1000k

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
 
#include "decoder.h"
#include "r_device.h"
#include "rtl_433.h"
#include "bitbuffer.h"
#include <stdint.h>

// --- Configuration ---
#define DECODED_PAKET_LEN_BYTES (41)
static const uint32_t BITTIME_US = 50;

// values for 200 kHz sample rate, need to be adapted to actual sample rate
#define PAKET_MIN_BITS (90)
#define PAKET_MAX_BITS (PAKET_MIN_BITS * 5 / 2) // NRZ encoding, stuffing and noise

static int ec3_decode_row(r_device *const decoder, const bitrow_t row, const uint16_t row_bits, const pulse_data_t *pulses);
static int ec3k_decode(r_device *decoder, bitbuffer_t *bitbuffer, const pulse_data_t *pulses);
static uint16_t calc_ec3k_crc(uint8_t *buffer, size_t len);
static uint16_t update_ec3k_crc(uint16_t crc, uint8_t ch);

static inline uint8_t bit_at(const uint8_t *bytes, int32_t bit)
{
    return (uint8_t)(bytes[bit >> 3] >> (7 - (bit & 7)) & 1);
}

static inline uint8_t symbol_at(const uint8_t *bytes, int32_t bit)
{
    // NRZI decoding
    uint8_t bit0 = (bit > 0) ? bit_at(bytes, bit - 1) : 0;
    uint8_t bit1 = bit_at(bytes, bit);
    return (bit0 == bit1) ? 1 : 0;
}

// maximum 8 nibbles (32 bits)
static uint32_t unpack_nibbles(const uint8_t* buf, int32_t start_nibble, int32_t num_nibbles)
{
    uint32_t val = 0;
    for (int32_t i = 0; i < num_nibbles; i++)
    {
        val = (val << 4) | ((buf[(start_nibble + i) / 2] >> ((1 - ((start_nibble + i) & 1)) * 4)) & 0x0F);
    }
    return val;
}

static int ec3_decode_row(r_device *const decoder, const bitrow_t row, const uint16_t row_bits, __attribute_maybe_unused__ const pulse_data_t *pulses) {
    int rc = DECODE_ABORT_EARLY;

#if 0
    int32_t diffFreq = (int32_t)(pulses->freq2_hz - pulses->freq1_hz + 0.5f);
    printf("#f1=%d f2=%d diff=%d ", (int32_t)(pulses->freq1_hz - pulses->centerfreq_hz + 0.5f), (int32_t)(pulses->freq2_hz - pulses->centerfreq_hz + 0.5f), diffFreq);
    printf("#RowLen=%-4i ", row_bits);
    for (int i = 0; i < row_bits; i++) {
        printf("%i", bit_at((const uint8_t*)row, i));
    }
    printf("\n");

    printf("#PulseLen=%-4i ", pulses->num_pulses);
    for (unsigned int i = 0; i < pulses->num_pulses; i++) {
        printf(" +%d -%d", pulses->pulse[i], pulses->gap[i]);
    }
    printf("\n");
#if 0
    printf("#RowLen=%-4i ", row_bits);
    for (int i = 0; i < (row_bits + 7) / 8; i++) {
        printf("%x", row[i]);
    }
    printf("\n");
#endif
#endif

    uint8_t packetbuffer[DECODED_PAKET_LEN_BYTES];
    int32_t packetpos = 0;
    uint8_t packet = 0;
    uint8_t onecount = 0;
    uint8_t recbyte = 0;
    uint8_t recpos = 0;

    for (int32_t bufferpos = 17; bufferpos < row_bits; bufferpos++)
    {
        uint8_t out = symbol_at((const uint8_t*)row, bufferpos);
        if (bufferpos > 17)
            out = out ^ symbol_at((const uint8_t*)row, bufferpos - 17);
        if (bufferpos > 12)
            out = out ^ symbol_at((const uint8_t*)row, bufferpos - 12);

        if (out)
        {
            if((onecount < 6) && (packetpos < DECODED_PAKET_LEN_BYTES))
            {
                onecount++;
                recbyte = recbyte >> 1 | 0x80;
                recpos++;
                if ((recpos == 8) && (packet))
                {
                    recpos = 0;
                    packetbuffer[packetpos++] = recbyte;
                }
            }
            else
            {
                // reset state to re-sync
                packet = 0;
                packetpos = 0;
                recpos = 0;
                onecount = 0;
            }
        }
        else
        {
            if ((onecount < 5) && (packetpos < DECODED_PAKET_LEN_BYTES))
            {
                // normal 0 bit
                recbyte = recbyte >> 1;
                recpos++;
                if ((recpos == 8) && (packet))
                {
                    recpos = 0;
                    packetbuffer[packetpos++] = recbyte;
                }
            }
            else if (onecount == 5)
            {
                // bit unstuffing: 0 after 5 ones is a stuffed 0, skip it
            }
            // start and end of packet is marked by 6 ones surrounded by 0 (0x7e)
            else if (onecount == 6)
            {
                packet = !packet;
                packetpos = 0;
                recpos = 0;
            }
            else
            {
                // reset state to re-sync
                packet = 0;
                packetpos = 0;
                recpos = 0;
            }

            onecount = 0;
        }

        if (packetpos >= DECODED_PAKET_LEN_BYTES)
        {
            // decode received ec3k packet
            uint16_t id              = unpack_nibbles(packetbuffer, 1, 4);
            uint16_t time_total_low  = unpack_nibbles(packetbuffer, 5, 4);
            uint16_t pad_1           = unpack_nibbles(packetbuffer, 9, 4);
            uint16_t time_on_low     = unpack_nibbles(packetbuffer, 13, 4);
            uint32_t pad_2           = unpack_nibbles(packetbuffer, 17, 7);
            uint32_t energy_low      = unpack_nibbles(packetbuffer, 24, 7);
            double   power_current   = unpack_nibbles(packetbuffer, 31, 4) / 10.0;
            double   power_max       = unpack_nibbles(packetbuffer, 35, 4) / 10.0;
            // unknown? (seems to be used for internal calculations)
            uint32_t energy2        = unpack_nibbles(packetbuffer, 39, 6);
            // 						nibbles[45:59]
            uint16_t time_total_high = unpack_nibbles(packetbuffer, 59, 3);
            uint32_t pad_3           = unpack_nibbles(packetbuffer, 62, 5);
            uint64_t energy_high     = (uint64_t)unpack_nibbles(packetbuffer, 67, 4) << 28;
            uint16_t time_on_high    = unpack_nibbles(packetbuffer, 71, 3);
            uint8_t  reset_counter   = unpack_nibbles(packetbuffer, 74, 2);
            uint8_t  flags           = unpack_nibbles(packetbuffer, 76, 1);
            uint8_t  pad_4           = unpack_nibbles(packetbuffer, 77, 1);
            uint16_t received_crc    = 0xffff ^ (unpack_nibbles(packetbuffer, 78, 2) | (unpack_nibbles(packetbuffer, 80, 2) << 8)); // little-endian
            uint16_t calculated_crc  = calc_ec3k_crc(packetbuffer, DECODED_PAKET_LEN_BYTES - 2);

            // convert to common units
            uint64_t energy_Ws = energy_high | energy_low;
            const double energy_kWh = energy_Ws / (1000.0 * 3600.0); // Ws to kWh
            const double energy2_kWh = energy2 / (1000.0 * 3600.0); // Ws to kWh
            uint32_t time_total = (uint32_t)time_total_low | ((uint32_t)time_total_high << 16);
            uint32_t time_on = (uint32_t)time_on_low | ((uint32_t)time_on_high << 16);

            if(pad_1 == 0 && pad_2 == 0 && pad_3 == 0 && pad_4 == 0) {
                if(calculated_crc == received_crc) {
                    /* clang-format off */
                    data_t *data = data_make(
                        "model",            "",              DATA_STRING, "Voltcraft Energy Count 3000",
                        "id",               "",              DATA_FORMAT, "%04x", DATA_INT, id,
                        "power",            "Power",         DATA_DOUBLE, power_current,
                        "energy",           "Energy",        DATA_DOUBLE, energy_kWh,
                        "energy2",          "Energy 2",      DATA_DOUBLE, energy2_kWh,
                        "mic",              "Integrity",     DATA_STRING, "CRC",
                        "time_total",       "Time total",    DATA_INT,    time_total,
                        "time_on",          "Time on",       DATA_INT,    time_on,
                        "power_max",        "Power max",     DATA_DOUBLE, power_max,
                        "reset_counter",    "Reset counter", DATA_INT,    reset_counter,
                        "flags",            "Flags",         DATA_INT,    flags,
                        NULL);
                    /* clang-format on */

                    decoder_output_data(decoder, data);
                    rc = 1;
                    break;
                }
                else {
                    decoder_logf(decoder, 1, __func__, "Warning: CRC error, calculated %04X but received %04X", calculated_crc, received_crc);
                    rc = DECODE_FAIL_MIC;
                }
            } else {
                decoder_logf(decoder, 1, __func__, "Warning: padding bits are not zero, pad_1=%u pad_2=%u pad_3=%u pad_4=%u", pad_1, pad_2, pad_3, pad_4);
                rc = DECODE_FAIL_SANITY;
            }

            // reset state to re-sync
            packet = 0;
            packetpos = 0;
            recpos = 0;
            onecount = 0;
        }
    }

    return rc;
}


static int ec3k_decode(r_device *decoder, bitbuffer_t *bitbuffer, const pulse_data_t *pulses)
{
    int rc = DECODE_ABORT_EARLY;
    // const int32_t diffFreq = (int32_t)(pulses->freq2_hz - pulses->freq1_hz + 0.5f);

    // TODO: remove
    // temporarily set verbose level high to get bitrow output
    // int orig_verbose = decoder->verbose;
    // int orig_verbose_bits = decoder->verbose;
    decoder->verbose = 3;
    decoder->verbose_bits = 3;

    const uint16_t min_row_bits = PAKET_MIN_BITS; // adapted to sample rate; TODO: samplerate adaption should not be necessary
    // const uint16_t min_row_bits = (PAKET_MIN_BITS * pulses->sample_rate) / 200000; // adapted to sample rate; TODO: samplerate adaption should not be necessary
    // const uint16_t max_row_bits = (PAKET_MAX_BITS * pulses->sample_rate) / 200000; // adapted to sample rate; TODO: samplerate adaption should not be necessary
    if (       bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < min_row_bits
            // || bitbuffer->bits_per_row[0] > max_row_bits
        )
    {
        decoder_logf(decoder, 3, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        rc = DECODE_ABORT_LENGTH; // Unrecognized data
    }
    // else if (diffFreq < 20000 || diffFreq > 110000)
    // {
    //     decoder_logf(decoder, 3, __func__, "frequency shift %d out of range", diffFreq);
    //     rc = DECODE_ABORT_EARLY; // Unrecognized data
    // }
    else
    {
        rc = ec3_decode_row(decoder, bitbuffer->bb[0], bitbuffer->bits_per_row[0], pulses);
    }

    // TODO: remove
    // decoder->verbose = orig_verbose;
    // decoder->verbose = orig_verbose_bits;

    return rc;
}

// from the ec3k python implementation at https://github.com/avian2/ec3k
static uint16_t calc_ec3k_crc(uint8_t *buffer, size_t len)
{
    uint16_t crc = 0xffff;
    for(size_t i = 0; i < len; i++) {
        crc = update_ec3k_crc(crc, buffer[i]);
    }
    return crc;
}

static uint16_t update_ec3k_crc(uint16_t crc, uint8_t ch)
{
    ch ^= crc & 0xff;
    ch ^= (ch << 4) & 0xff;
    return (((uint16_t)ch << 8) | (crc >> 8)) ^ ((uint16_t)ch >> 4) ^ ((uint16_t)ch << 3);
}


/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char const *const output_fields[] = {
        "model",
        "id",
        "power",
        "energy",
        "energy2",
        "time_total",
        "time_on",
        "power_max",
        "reset_counter",
        "flags",
        "mic",
        NULL,
};

const r_device ec3k = {
    .name           = "Voltcraft Energy Count 3000",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = BITTIME_US,
    .long_width     = BITTIME_US,
    .tolerance      = BITTIME_US / 10, // in us ; there can be up to 5 consecutive 0 or 1 pulses and the sync word is 6 bits, so 15% would be max
    .gap_limit      = 3000,  // some distance above long
    .reset_limit    = 5000, // a bit longer than packet gap
    .decode_fn      = &ec3k_decode,
    .disabled       = 0,
    .fields         = output_fields,
};
