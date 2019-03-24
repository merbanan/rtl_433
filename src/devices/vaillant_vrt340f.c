#include "decoder.h"

// Protocol of the Vaillant VRT 340f (calorMatic 340f) central heating control
//     http://wiki.kainhofer.com/hardware/vaillantvrt340f
// The data is sent differential Manchester encoded
// with bit-stuffing (after five 1 bits an extra 0 bit is inserted)
//
// All bytes are sent with least significant bit FIRST (1000 0111 = 0xE1)
//
//  0x00 00 7E | 6D F6 | 00 20 00 | 00 | 80 | B4 | 00 | FD 49 | FF 00
//    SYNC+HD. | DevID | CONST?   |Rep.|Wtr.|Htg.|Btr.|Checksm| EPILOGUE
//
// CONST? ... Unknown, but constant in all observed signals
// Rep.   ... Repeat indicator: 0x00=original signal, 0x01=first repeat
// Wtr.   ... pre-heated Water: 0x80=ON, 0x88=OFF (bit 8 is always set)
// Htg.   ... Heating: 0x00=OFF, 0xB4=ON (2-point), 0x01-0x7F=target heating water temp
//              (bit 8 indicates 2-point heating mode, bits 1-7 the heating water temp)
// Btr.   ... Battery: 0x00=OK, 0x01=LOW
// Checksm... Checksum (2-byte signed int): = -sum(bytes 4-12)

// Copyright (C) 2017 Reinhold Kainhofer <reinhold@kainhofer.com>
// License: GPL v2+ (or at your choice, any other OSI-approved Open Source license)

static int16_t
calculate_checksum(uint8_t *buff, int from, int to) {
    int16_t checksum = 0;
    for (int byteCnt = from; byteCnt <= to; byteCnt++) {
        checksum += (int16_t)buff[byteCnt];
    }
    return -checksum;
}

static int
validate_checksum(r_device *decoder, uint8_t * msg, int from, int to, int cs_from, int cs_to)
{
    // Fields cs_from and cs_to hold the 2-byte checksum as signed int
    int16_t expected = msg[cs_from]*0x100+ msg[cs_to];
    int16_t calculated = calculate_checksum(msg, from, to);

    if (expected != calculated) {
        if (decoder->verbose) {
            fprintf(stderr, "Checksum error in Vaillant VRT340f.  Expected: %04x  Calculated: %04x\n", expected, calculated);
            fprintf(stderr, "Message (data content of bytes %d-%d): ", from, to);
            bitrow_print(&msg[from], (to - from + 1) * 8);
        }
    }
    return expected == calculated;
}


static uint16_t
get_device_id(uint8_t * msg, int pos)
{
    uint16_t deviceID = msg[pos]*0x100 + msg[pos+1];
    return deviceID;
}

static uint8_t
get_heating_mode(uint8_t * msg)
{
    uint8_t mode = 0;
    uint8_t tmp = msg[10];
    if (tmp==0) {
        mode = 0;
    } else if (tmp >> 7) { // highest bit set => automatic (2-point) mode
        mode = 1;
    } else { // highest bit not set, but value given => analogue mode (bits 1-7 hold temperature)
        mode = 2;
    }
    return mode;
}

static uint8_t
get_target_temperature(uint8_t * msg)
{
    uint8_t temp = (msg[10] & 0x7F); // highest bit indicates auto(2-point) / analogue mode
    return temp;
}

static uint8_t
get_water_preheated(uint8_t * msg)
{
    uint8_t water = (msg[9] & 8) == 0; // bit 4 indicates water: 0=ON, 1=OFF
    return water; // if not zero, water is pre-heated
}

static uint8_t
get_battery_status(uint8_t * msg)
{
    uint8_t status = msg[11] != 0; // if not zero, battery is low
    return status;
}

static int
vaillant_vrt340_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;

    data_t *data;

    // TODO: Use repeat signal for error checking / correction!

    // each row needs to have at least 128 bits (plus a few more due to bit stuffing)
    if (bitbuffer->bits_per_row[0]<128)
        return 0;

    // The protocol uses bit-stuffing => remove 0 bit after five consecutive 1 bits
    // Also, each byte is represented with least significant bit first -> swap them!
    bitbuffer_t bits = {0};
    int ones = 0;
    for (uint16_t k = 0; k < bitbuffer->bits_per_row[0]; k++) {
        int b = bitrow_get_bit(bb[0], k);
        if (b==1) {
            bitbuffer_add_bit(&bits, 1);
            ones++;
        } else {
            if (ones != 5) { // Ignore a 0 bit after five consecutive 1 bits:
                bitbuffer_add_bit(&bits, 0);
            }
            ones = 0;
        }
    }

    uint16_t bitcount = bits.bits_per_row[0];

    // Change to least-significant-bit last (protocol uses least-significant-bit first) for hex representation:
    for (uint16_t k = 0; k <= (uint16_t)(bitcount-1)/8; k++) {
        bits.bb[0][k] = reverse8(bits.bb[0][k]);
    }
    bb = bits.bb;

    /* DEBUG: print out the received packet */
    //fprintf (stderr, "Vaillant ");
    //bitrow_print(bb[0], bitcount);

    // A correct message has 128 bits plus potentially two extra bits for clock sync at the end
    if(!(128 <= bitcount && bitcount <= 131) && !(168 <= bitcount && bitcount <= 171))
        return 0;

    // "Normal package":
    if ((bb[0][0] == 0x00) && (bb[0][1] == 0x00) && (bb[0][2] == 0x7e) && (128 <= bitcount && bitcount <= 131)) {

        if (!validate_checksum(decoder, bb[0], /* Data from-to: */3,11, /*Checksum from-to:*/12,13)) {
            return 0;
        }

        // Device ID starts at bit 4:
        uint16_t deviceID = get_device_id(bb[0], 3);
        uint8_t heating_mode = get_heating_mode(bb[0]); // 0=OFF, 1=ON (2-point heating), 2=ON (analogue heating)
        uint8_t target_temperature = get_target_temperature(bb[0]);
        uint8_t water_preheated = get_water_preheated(bb[0]); // 1=Pre-heat, 0=no pre-heated water
        uint8_t isBatteryLow = get_battery_status(bb[0]);

        data = data_make(
                "model",   "", DATA_STRING, _X("Vaillant-VRT340f","Vaillant VRT340f Central Heating Thermostat"),
                _X("id","device"),  "Device ID", DATA_FORMAT, "0x%04X", DATA_INT, deviceID,
                "heating", "Heating Mode", DATA_STRING, (heating_mode==0)?"OFF":((heating_mode==1)?"ON (2-point)":"ON (analogue)"),
                "heating_temp", "Heating Water Temp.", DATA_FORMAT, "%d", DATA_INT, (int16_t)target_temperature,
                "water",   "Pre-heated Water", DATA_STRING, water_preheated ? "ON" : "off",
                "battery", "Battery", DATA_STRING, isBatteryLow ? "Low" : "Ok",
                NULL);
        decoder_output_data(decoder, data);

        return 1;
    }

    // "RF detection package":
    if ((bb[0][0] == 0x00) && (bb[0][1] == 0x00) && (bb[0][2] == 0x7E) && (168 <= bitcount && bitcount <= 171)) {

        if (!validate_checksum(decoder, bb[0], /* Data from-to: */3,16, /*Checksum from-to:*/17,18)) {
            return 0;
        }

        // Device ID starts at bit 12:
        uint16_t deviceID = get_device_id(bb[0], 11);

        data = data_make(
                "model",   "", DATA_STRING, _X("Vaillant-VRT340f","Vaillant VRT340f Central Heating Thermostat (RF Detection)"),
                _X("id","device"),  "Device ID", DATA_INT, deviceID,
                NULL);
        decoder_output_data(decoder, data);

        return 1;
    }

    return 0;
}

static char *output_fields[] = {
    "model",
    "device", // TODO: delete this
    "id",
    "heating",
    "heating_temp",
    "water",
    "battery",
    NULL
};

r_device vaillant_vrt340f = {
    .name           = "Vaillant calorMatic VRT340f Central Heating Control",
    .modulation     = OOK_PULSE_DMC,
    .short_width    = 836,  // half-bit width 836 us
    .long_width     = 1648, // bit width 1648 us
    .reset_limit    = 4000,
    .tolerance      = 120, // us
    .decode_fn      = &vaillant_vrt340_callback,
    .disabled       = 0,
    .fields         = output_fields
};
