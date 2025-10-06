/** @file
    Decoder for Voltcraft EnergyCount 3000 (ec3k, sold by Conrad) / Technoline Cost Control RT-110 , tested with:
      - Technoline Cost Control RT-110 (https://www.technotrade.de/produkt/technoline-cost-control-rt-110/), EAN 4029665006208

    Should also work with these devices from other manufacturers that use the same protocol or are even identical:
      - Voltcraft ENERGYCOUNT 3000 ENERGY LOGGER (Item No. 12 53 53, https://conrad-rus.ru/images/stories/virtuemart/media/125353-an-01-ml-TCRAFT_ENERGYC_3000_ENER_MESSG_de_en_nl.pdf)
        (don't mix that up with similar products from the same company like "Energy Check 3000", "Energy Monitor 3000" and "Energy Logger 4000")
      - Velleman (type NETBESEM4)
      - La Crosse Techology Remote Cost Control Monitor” (type RS3620)

    Copyright (C) 2025 Michael Dreher <michael(a)5dot1.de>, nospam2000 at github.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define DECODED_PAKET_LEN_BYTES (41)
static const uint32_t BITTIME_US = 50;
#define PAKET_MIN_BITS (90)
#define PAKET_MAX_BITS (PAKET_MIN_BITS * 5 / 2) // NRZ encoding, stuffing and noise

static int ec3k_extract_fields(r_device *const decoder, const uint8_t *packetbuffer);
static uint16_t calc_ec3k_crc(const uint8_t *buffer, size_t len);

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

static inline uint8_t descrambled_symbol_at(const uint8_t *bytes, int32_t bit)
{
    uint8_t out = symbol_at(bytes, bit);
    if (bit > 17)
        out = out ^ symbol_at(bytes, bit - 17);
    if (bit > 12)
        out = out ^ symbol_at(bytes, bit - 12);

    return out;
}

// maximum 8 nibbles (32 bits)
static inline uint32_t unpack_nibbles(const uint8_t *buf, int32_t start_nibble, int32_t num_nibbles)
{
    uint32_t val = 0;
    for (int32_t i = 0; i < num_nibbles; i++) {
        val = (val << 4) | ((buf[(start_nibble + i) / 2] >> ((1 - ((start_nibble + i) & 1)) * 4)) & 0x0F);
    }
    return val;
}

/**
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
    The packet length is 41 bytes (including the 16-bit CRC but excluding the two framing bytes and the optional flag byte).
    The packet is NRZI encoded, with bit stuffing (a 0 is inserted after 5 consecutive 1 bits).
    The packet is framed by 0x7E (01111110) bytes at start and end.
    The CRC is calculated over the packet excluding the leading and trailing framing byte 0x7E and the crc-value itself.
    The CRC bytes in the packet are in little-endian order (low byte first). I didn't find the parameters
    for a standard implementation, so I took the implementation from the python code at https://github.com/avian2/ec3k.

    The following fields are decoded:
        id -- 16-bit ID of the device
        time_total -- time in seconds since last reset
        time_on -- time in seconds since last reset with non-zero device power
        energy -- total energy in kWh (transmitted in Ws (watt-seconds))
        power_current -- current device power in watts (transmitted in 0.1 watt steps)
        power_max -- maximum device power in watts (reset at unknown intervals, transmitted in 0.1 watt steps)
        reset_counter -- total number of transmitter resets
        device_on_flag -- true if device is currently drawing non-zero power
        crc
        some padding fields that are always zero

    Decoding works best with this params for a RTL28382U, you might need to tune the frequency offset to your devices, especially for 250k sample rate:
        rtl_433 -f 868000k -s 1000k
        rtl_433 -f 868300k -s 250k

    \verbatim
    To test with a file created by URH you can use this command:
        cat Rad1o-20251001_112936-868_2MHz-2MSps-2MHz_single.complex16s | csdr convert_s8_f | csdr fir_decimate_cc 2 0.02 HAMMING | csdr convert_f_s8 | rtl_433 -R 282 -r CS8:- -f 868000k -s 1000k
        fir_decimate_cc: taps_length = 201
        rtl_433 version -128-NOTFOUND branch feat-ec3k at 202510042209 inputs file rtl_tcp RTL-SDR with TLS
        New defaults active, use "-Y classic -s 250k" if you need the old defaults

        [Input] Test mode active. Reading samples from file: <stdin>
        _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
        time      : @1.864588s
        model     : Voltcraft-EnergyCount3000            id        : bb9b
        Power     : 90.200       Energy    : 754.518       Energy 2  : 1.860         Integrity : CRC
        Time total: 64942080     Time on   : 57501776      Power max : 186.500       Reset counter: 4          Flags     : 8
        [pulse_slicer_pcm] Voltcraft-EnergyCount3000
        codes     : {550}d4018c7e67bf2e4b15f2b3b404fc2bdace27e30ba759a5be0edcbff0f5e2b070f59d89ec5459cef2a6cddb6adf8c4e487546309633d08e4a092fba1d16749519e5de63c5c0
    \endverbatim

    Check here for some example captures: https://github.com/merbanan/rtl_433_tests/tree/master/tests/ec3k/01
 */
static int ec3k_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int rc = DECODE_ABORT_EARLY;

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] < PAKET_MIN_BITS) {
        decoder_logf(decoder, 3, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        rc = DECODE_ABORT_LENGTH; // Unrecognized data
    }
    else {
        uint8_t packetbuffer[DECODED_PAKET_LEN_BYTES];
        int32_t packetpos = 0;
        uint8_t packet    = 0;
        uint8_t onecount  = 0;
        uint8_t recbyte   = 0;
        uint8_t recpos    = 0;

        for (int32_t bufferpos = 17; rc != 1 && bufferpos < bitbuffer->bits_per_row[0]; bufferpos++) {
            uint8_t out = descrambled_symbol_at((const uint8_t *)bitbuffer->bb[0], bufferpos);
            if (out) {
                if ((onecount < 6) && (packetpos < DECODED_PAKET_LEN_BYTES)) {
                    onecount++;
                    recbyte = recbyte >> 1 | 0x80;
                    recpos++;
                    if ((recpos == 8) && (packet)) {
                        recpos                    = 0;
                        packetbuffer[packetpos++] = recbyte;
                    }
                }
                else {
                    // reset state to re-sync
                    packet    = 0;
                    packetpos = 0;
                    recpos    = 0;
                    onecount  = 0;
                }
            }
            else {
                if ((onecount < 5) && (packetpos < DECODED_PAKET_LEN_BYTES)) {
                    // normal 0 bit
                    recbyte = recbyte >> 1;
                    recpos++;
                    if ((recpos == 8) && (packet)) {
                        recpos                    = 0;
                        packetbuffer[packetpos++] = recbyte;
                    }
                }
                else if (onecount == 5) {
                    // bit unstuffing: 0 after 5 ones is a stuffed 0, skip it
                }
                // start and end of packet is marked by 6 ones surrounded by 0 (0x7e)
                else if (onecount == 6) {
                    packet    = !packet;
                    packetpos = 0;
                    recpos    = 0;
                }
                else {
                    // reset state to re-sync
                    packet    = 0;
                    packetpos = 0;
                    recpos    = 0;
                }

                onecount = 0;
            }

            if (packetpos >= DECODED_PAKET_LEN_BYTES) {
                rc = ec3k_extract_fields(decoder, packetbuffer);

                // reset state to re-sync
                packet    = 0;
                packetpos = 0;
                recpos    = 0;
                onecount  = 0;
            }
        }
    }

    return rc;
}

static int ec3k_extract_fields(r_device *const decoder, const uint8_t *packetbuffer)
{
    int rc = DECODE_FAIL_SANITY;

    // decode received ec3k packet
    uint16_t id             = unpack_nibbles(packetbuffer, 1, 4);
    uint16_t time_total_low = unpack_nibbles(packetbuffer, 5, 4);
    uint16_t pad_1          = unpack_nibbles(packetbuffer, 9, 4);
    uint16_t time_on_low    = unpack_nibbles(packetbuffer, 13, 4);
    uint32_t pad_2          = unpack_nibbles(packetbuffer, 17, 7);
    uint32_t energy_low     = unpack_nibbles(packetbuffer, 24, 7);
    double power_current    = unpack_nibbles(packetbuffer, 31, 4) / 10.0;
    double power_max        = unpack_nibbles(packetbuffer, 35, 4) / 10.0;
    // unknown? (seems to be used for internal calculations)
    uint32_t energy2 = unpack_nibbles(packetbuffer, 39, 6);
    // 						nibbles[45:59]
    uint16_t time_total_high = unpack_nibbles(packetbuffer, 59, 3);
    uint32_t pad_3           = unpack_nibbles(packetbuffer, 62, 5);
    uint64_t energy_high     = (uint64_t)unpack_nibbles(packetbuffer, 67, 4) << 28;
    uint16_t time_on_high    = unpack_nibbles(packetbuffer, 71, 3);
    uint8_t reset_counter    = unpack_nibbles(packetbuffer, 74, 2);
    uint8_t flags            = unpack_nibbles(packetbuffer, 76, 1);
    uint8_t pad_4            = unpack_nibbles(packetbuffer, 77, 1);
    uint16_t received_crc    = 0xffff ^ (unpack_nibbles(packetbuffer, 78, 2) | (unpack_nibbles(packetbuffer, 80, 2) << 8)); // little-endian
    uint16_t calculated_crc  = calc_ec3k_crc(packetbuffer, DECODED_PAKET_LEN_BYTES - 2);

    // convert to common units
    uint64_t energy_Ws       = energy_high | energy_low;
    const double energy_kWh  = energy_Ws / (1000.0 * 3600.0); // Ws to kWh
    const double energy2_kWh = energy2 / (1000.0 * 3600.0);   // Ws to kWh
    uint32_t time_total      = (uint32_t)time_total_low | ((uint32_t)time_total_high << 16);
    uint32_t time_on         = (uint32_t)time_on_low | ((uint32_t)time_on_high << 16);

    if (pad_1 == 0 && pad_2 == 0 && pad_3 == 0 && pad_4 == 0) {
        if (calculated_crc == received_crc) {
            /* clang-format off */
            data_t *data = data_make(
                "model",            "",              DATA_STRING, "Voltcraft-EnergyCount3000",
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
        }
        else {
            decoder_logf(decoder, 1, __func__, "Warning: CRC error, calculated %04X but received %04X", calculated_crc, received_crc);
            rc = DECODE_FAIL_MIC;
        }
    }
    else {
        decoder_logf(decoder, 1, __func__, "Warning: padding bits are not zero, pad_1=%u pad_2=%u pad_3=%u pad_4=%u", pad_1, pad_2, pad_3, pad_4);
        rc = DECODE_FAIL_SANITY;
    }

    return rc;
}

// copied from the ec3k python implementation at https://github.com/avian2/ec3k
static inline uint16_t calc_ec3k_crc(const uint8_t *buffer, size_t len)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = buffer[i];
        ch ^= crc & 0xff;
        ch ^= (ch << 4) & 0xff;
        crc = (((uint16_t)ch << 8) | (crc >> 8)) ^ ((uint16_t)ch >> 4) ^ ((uint16_t)ch << 3);
    }
    return crc;
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

r_device const ec3k = {
        .name        = "Voltcraft-EnergyCount3000",
        .modulation  = FSK_PULSE_PCM,
        .short_width = BITTIME_US,
        .long_width  = BITTIME_US,
        .tolerance   = BITTIME_US / 10, // in us ; there can be up to 5 consecutive 0 or 1 pulses and the sync word is 6 bits, so 15% would be max
        .gap_limit   = 3000,            // some distance above long
        .reset_limit = 5000,            // a bit longer than packet gap
        .decode_fn   = &ec3k_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
