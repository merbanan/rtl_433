/** @file
    Bresser SmartHome Garden set.

    Copyright (C) 2024 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int bresser_garden_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Bresser SmartHome Garden set (Fujian Baldr / Homgar family, also sold as RainPoint). 433.92 MHz
FSK PCM. A bidirectional irrigation set. Each frame is reported under the model of its actual
transmitter, taken from the top byte of the 32-bit id (the device class):

    model                  class  Bresser              Homgar (Baldr)     RainPoint
    ---------------------  -----  -------------------  -----------------  -----------------------
    Bresser-Gateway        0x01   HWS388WRF-V7 7510100 HWS388             TWG004WRF (Wi-Fi hub)
    Bresser-WaterTimer     0x1F   HTV103FRF 7910100    HTV103 (1-zone)    ITV0103W / TTV1013WRF
                                  HTV203FRF 7910101    HTV203 (2-zone)    TTV203WRF (2-zone)
    Bresser-SoilMoisture   0x47   HCS005FRF 7910102    HCS005             ICS0001W
    Bresser-Garden          --    (fallback for an unrecognised class byte)

FCC: gateway 2AWDBHWS388WRF, soil sensor 2AWDBTCS005FRF. Issue #2988 (\@kami83), PR #3005.

Topology (the thermo-hygro unit is separate and already supported, so it is left out here):

    Soil Moisture (0x47) --0x09--> Water Timer (0x1F) --0x0a relay--> Gateway / base (0x01)
                                        |  The Water Timer has NO soil probe: it forwards the
                                        |  sensor's reading to the base in a new 0x0a message.
                                        |  It also stores the watering schedule and runs it on
                                        |  its own clock, emitting 0x04 watering events.
        base --0x85 config / 0x86 schedule table / 0x20 config counter--> Water Timer

Also in the set but NOT handled by this decoder:
- An outdoor thermo-hygro sensor (Homgar H666TH outdoor / H999TH indoor with LCD) that the base
  station reads; it transmits on the already-supported Bresser Thermo-/Hygro 3CH decoder
  (protocol 52), not here.
- The base station's barometric pressure: shown on the display and sent to the cloud only, never
  transmitted on 433 MHz.

The protocol:

- Bidirectional: messages go source -> target, then the target acknowledges back to the source.
  An acknowledgement's message type is the acked type with bit 7 set (0x0a -> 0x8a) and it repeats
  the acked message counter.
- The 32-bit id's top byte is the device class (0x01/0x1F/0x47), pattern CC00xxxx. The id is stable
  across a power cycle (the 0x01 INIT after power-up carries the same id with the counter reset to
  1); stability across a full battery *replacement* is assumed but not yet explicitly tested.
- Whatever the message type, the framed message is always 33 bytes after the preamble/sync word.
  The Soil Moisture Sensor prepends a long (~1250-bit) wake-up preamble so its frames reach
  ~1520 bits, but the decoded frame is still 33 bytes.

Flex decoder:

    rtl_433 -R 0 -X "n=Bresser_FSK,m=FSK_PCM,s=50,l=50,r=10000,bits>=40,bits<=1000,preamble=aaf3" -M level -Y minmax -Y magest -s 1200k 2>&1 | grep codes

    codes     : {298}e9105e51000000001f05004701010805ff4747000435030000000000000000000000007ab60
    codes     : {298}e9105e511f05004788160001018110000505e001b946ed110102000000000000000000ec640
    codes     : {298}e9105e51881600011f050047020307050988008527030000000000000000000000000067220
    codes     : {298}e9105e511f050047881600010283010000000000000000000000000000000000000000dcc90

Data layout (example frames from PR #3005's household; "?" marks a byte that was unknown to the
original author - several are decoded below):

    Byte Position                   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33
               preample syncword   TT TT TT TT SS SS SS SS RR AC LL MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM MM ZZ ZZ XX
                                                                    ID ?? ?? ?? ?? ?? FF ??
    Sensor INIT aaaaaaa f3e9105e51 00 00 00 00 1f 05 00 47 01 01 08 05 ff 47 47 00 04 35 03 00 00 00 00 00 00 00 00 00 00 00 00 7a b6 0
                                                                    ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??
    Base acknowledgemt    e9105e51 1f 05 00 47 88 16 00 01 01 81 10 00 05 05 e0 01 b9 46 ed 11 01 02 00 00 00 00 00 00 00 00 00 ec 64 0
                                                                    ID BB 88 HH 85 TEMP
    Sensor Send T/H       e9105e51 88 16 00 01 1f 05 00 47 02 03 07 05 09 88 00 85 27 03 00 00 00 00 00 00 00 00 00 00 00 00 00 67 22 0

    Base acknowledge T/H  e9105e51 1f 05 00 47 88 16 00 01 02 83 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 dc c9 0
                                                                    ID?05 ?? BB 88 HH 85 TEMP
    Water to Base         e9105e51 88 16 00 01 52 12 00 1f 0f 0a 09 0b 05 10 09 88 00 85 10 03 00 00 00 00 00 00 00 00 00 00 00 8f 1b 0

Frame envelope (common to every message; the "sub-message" is the type-specific inner payload):

- TT:{32} Target id, little-endian (0x00000000 during the init/pairing broadcast).
- SS:{32} Source id, little-endian, hard-coded (top byte = device class).
- RR: {8} Message counter (increments per message; an acknowledgement repeats the acked value).
- MT: {8} Message type. Bit 7 (0x80) is the REPLY flag: a reply's type is the request's type with
          bit 7 set (0xNN -> 0x8N) and it echoes the request's counter. The reply is either an empty
          acknowledgement (when the data was in the request, e.g. 0x09 -> 0x89) or carries the
          requested data (when the request was a bare poll, e.g. 0x02 -> 0x82). The low 7 bits
          identify the sub-message (0x01 init, 0x03/0x09/0x0a telemetry, 0x04 watering,
          0x02/0x05/0x06 Water Timer polls, 0x85/0x86/0x20 config).
- LL: {8} Sub-message length.
- MM:{..} Sub-message (see below).
- ZZ:{16} CRC-16, poly 0x1021, init 0xd636.
- XX: Trailing bit.

0x01 - INIT / pairing broadcast (VERIFIED, seen on power-up)

- Soil sensor -> target 0x00000000 (broadcast); the counter resets to 1.
- ID:{8} device type (0x0e on HCS005FRF, 0x05 on the original author's unit),
- FF:{8} firmware, 0x35 = 53 (matches the app).

0x81 - acknowledgement of the INIT (UNCONFIRMED)

- Payload varies between captures. This was the date/time-sync candidate, but 0x82 (below) turned
  out to be the 0x02-poll reply, so 0x81's role is unclear. Left as Unknown msg.

0x02 / 0x05 / 0x06 - Water Timer -> gateway polls (VERIFIED via reply pairing, see Bit 7 above)

- The Water Timer polls the gateway; the gateway replies with type|0x80 (0x82/0x85/0x86), same
  counter, ~0.07 s later. 0x05 (get-config) and 0x06 (get-schedule, byte[2] = page 0/1) are bare
  2-3 byte requests; 0x02 (status request) is longer (len 15) and carries the Water Timer's own
  state. Payloads emitted raw (msg), not yet field-decoded; reported as "Status/Config/Schedule
  request".
- Polling is autonomous, not gateway-gated (VERIFIED, 2026-07-21 gateway power-off): with the
  gateway off for 46 min the Water Timer kept sending 0x02 on a fixed ~9.4 min timer (gaps 9m20s,
  9m20s, 9m41s) and simply got no reply, while its other frames continued (so the valve was alive
  and reception was fine). Only 0x02 is periodic - no 0x05/0x06 fired in that window, so those two
  look event-driven (a boot/config resync) rather than timed.

0x82 - gateway's reply to the Water Timer's 0x02 status request (VERIFIED: 17/17 captured 0x02
  polls were answered by an 0x82 with the same counter, ~0.07 s later; 0 unsolicited)

- gateway_time = bytes[2:5] (little-endian u24) is a monotonic counter the base keeps cloud-synced:
  pulling the base's batteries reset it to 0 and it re-synced from the cloud on boot (it does NOT
  free-run like an uptime timer), ticking at ~1.07/s. It behaves like a wall clock, but its exact
  unit is unconfirmed, so the raw counter is emitted and the status is the generic "Status response"
  rather than claiming "time". byte[1] increments slowly (a frame/epoch counter).

0x03 - Temp/Hum send (UNCONFIRMED: the original PR's frames show a direct soil -> base path; our
  set uses 0x09 soil -> Water Timer then the 0x0a relay instead)

- ID:{8} device type (0x05 on the author's unit),
- BB:{8} battery (0x09 full / 0x11 low; low nibble ~ level, high nibble low-battery flag),
- 0x88 moisture field marker, HH:{8} moisture %,
- 0x85 temperature field marker, TEMP:{16} temperature, little-endian, Fahrenheit x10.

0x83 / 0x89 / 0x8a - acknowledgements, empty (zero) payload. An ack's type is the acked type with
  bit 7 set: 0x8a acks the Water Timer's 0x0a (VERIFIED), 0x89 the soil 0x09, 0x83 the 0x03.

0x0a - Water Timer -> base telemetry, RELAYED soil reading (VERIFIED)

The Water Timer has no soil probe; this message relays the Soil Moisture Sensor's reading
(same battery / moisture / temperature block as the sensor's own 0x09 below).

- ID:{8} Water Timer device type / address (0x06 observed on HTV103FRF; identifies the unit).
- AD:{8} Address of the paired Soil Moisture Sensor (0x06 observed; matches the sensor's 0x09).
- ??:{8} Variable, value changes each message (per-message counter, meaning unknown).
- BB:{8} Battery information, 0x09 = Full battery, 0x11 = Low Battery.
         Last nibble probably the battery level, 1 for 3.6 / 3.8V , 9 for 4.5 V
         First nibble probably the low battery flag.
- 88:{8} Fixed value 0x88, moisture field marker.
- HH:{8} Moisture %, relayed from the Soil Moisture Sensor (moves continuously, not 0x00).
- 85:{8} Fixed value 0x85, temperature field marker.
- TEMP:{16} Temperature_F, little indian, scale 10.

0x09 - Soil Moisture Sensor -> Water Timer telemetry (VERIFIED)

The sensor's own reading before the Water Timer relays it as 0x0a. Layout TY AD 00 BB 88 HH 85 TL TH:

- TY:{8} Device type (0x0e on HCS005FRF, 0x05 on the original author's unit).
- AD:{8} Device address.
- BB:{8} Battery, same encoding as 0x0a.
- HH:{8} Moisture %, preceded by the 0x88 marker.
- TEMP:{16} Temperature_F, little indian, scale 10, preceded by the 0x85 marker.

0x04 - Water Timer -> base watering event (VERIFIED)

Sent when a scheduled or manual run actually waters (the timer skips a slot when the soil is
already wet enough), so it is seen at most about twice a day:

- AD:{8} Soil Moisture Sensor address.
- programme:{16} programme id, tracks the schedule slot (one value per daily slot).
- cycle_counter:{16} scheduled-slot counter, little-endian, monotonic; advances once per
  scheduled slot and NOT on manual runs (two manual runs on the same day read the same value).
- trigger:{8} high nibble = source (0x4 scheduled, 0x2 manual), low nibble = irrigation mode
  (0x1 normal, 0x2 misting). So 0x41 scheduled-normal, 0x21 manual-normal, 0x22 manual-misting.
- b[19]:{8} unknown (small; differs normal vs misting; possibly a segment/pulse count).
- duration:{8} the *actual* run duration in seconds, not the programmed value (a manual run
  hand-stopped at ~90 s reads 91; 0x3c = 60 matches the app default of 1 min).

0x85 - base -> Water Timer schedule/duration config (VERIFIED)

Pushed on each app settings change; decoded by setting distinctive values and diffing the payload:

- 00,
- default_duration:{16} default run duration, little-endian seconds (0x0078 = 120 = 2 min,
  0x495c = 18780 = 5 h 13 m),
- mist_run:{16} misting run-time, little-endian seconds,
- mist_interval:{16} misting interval, little-endian seconds,
- AD:{8} Soil Moisture Sensor address (0 = none),
- stop_moisture:{8} Stop-Plan moisture threshold percent (a scheduled plan is skipped while the
  soil is wetter than this),
- 00 00, b[11] unknown (0x42 observed), 00,
- flow_rate:{8} flow-rate calibration percent (the constant used to compute litres).

Note: the recurring 0x21 message ("0109013c000a…") is a periodic status/heartbeat whose 0x3c is a
fixed 60 s interval, NOT the watering duration (that lives here in 0x85). Educated-guess only.

0x86 - base -> Water Timer schedule table (VERIFIED)

A 1-byte header - the more_parts flag: 1 = another 0x86 page follows, 0 = last page (a table with
>2 plans splits across several 0x86 messages; confirmed by pushing a 5-plan table that split
2+2+1) - followed by one 7-byte record per watering plan. Per record:

- enabled(0x80) | weekday bitmask (bits 0-6 = Sun..Sat, used in weekly mode),
- minute | ((hour & 3) << 6),
- irrigation-type | (day_mode << 3) | (hour >> 2): type bit6 (0x40) normal / bit7 (0x80) misting;
  day_mode 1 = every day, 2 = odd, 3 = even, 4 = weekly,
- duration:{16} little-endian seconds,
- water_limit:{16} little-endian, units of 0.1 L (0 = no limit).
A misting plan's run-time / interval live in 0x85, not here.

0x20 - base config-change counter (VERIFIED)

A one-byte counter that increments by one on each settings change (e.g. 0x1e -> 0x1f); the base
station emits it around a config push.

0x21 - base -> Water Timer heartbeat (educated guess)

Periodic status ("0109013c000a…"); the 0x3c = 60 s is a fixed interval, not the watering duration
(that lives in 0x85). Gateway-dependent: vanished entirely when the base was powered off (Part C).

0xa0 / 0xa1 - beacon (educated guess)

Periodic, ack bit set; payload usually just 0x02 (0xa1) / 0x00 (0xa0), with a fuller payload
momentarily around a config push. Also gateway-dependent (vanished with the base off), so despite
the source id in the frame the exchange needs the gateway present.

The base power-off (Part C) also confirmed the Water Timer is autonomous: it stores the 0x86
schedule and runs it on its own clock (emitting 0x04) with the base gone; the base is only the
cloud gateway, config pusher and time (0x82) source. A disabled plan rides in the 0x86 table with
enabled=0 rather than being dropped, and day_mode 2/3 (odd/even) are confirmed via the app.

*/

/*
Each of the set's devices transmits with a distinct 32-bit id whose top byte is the device class
(0x47 soil sensor, 0x1f water timer, 0x01 gateway/base station). Report the model of the actual
transmitter, so e.g. a Water Timer beacon is Bresser-WaterTimer and a gateway config is
Bresser-Gateway, rather than one generic name for all control traffic. An unrecognised class falls
back to the generic "Bresser-Garden".
*/
static char const *bresser_garden_model(uint32_t source_id)
{
    switch (source_id >> 24) {
        case 0x47: return "Bresser-SoilMoisture";
        case 0x1f: return "Bresser-WaterTimer";
        case 0x01: return "Bresser-Gateway";
        default:   return "Bresser-Garden"; // fallback for an unrecognised device class
    }
}

static int bresser_garden_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0xaa, 0xf3, 0xe9, 0x10, 0x5e, 0x51};

    uint8_t b[33];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];

    // The Soil Moisture Sensor prepends a long wake-up preamble (~1250 bits of 0xaa),
    // so its frames reach ~1520 bits; the Gateway/Water Timer frames are <= ~630.
    if (msg_len > 2000) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    offset += sizeof(preamble_pattern) * 8;

    // guard AFTER skipping the preamble: we need a full 33-byte frame (264 bits) past it
    if ((msg_len - offset) < 33 * 8) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 33 * 8);

    if (crc16(b,33,0x1021,0xd636)) {
        decoder_logf(decoder, 1, __func__, "CRC error");
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, 33 * 8 , "MSG");

    //Extract info ...

    uint32_t target_id = (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0];
    uint32_t source_id = (b[7] << 24) | (b[6] << 16) | (b[5] << 8) | b[4];
    int counter        = b[8];
    int msg_type       = b[9];
    int msg_length     = b[10];
    int acknowledgement = (msg_type & 0xf0) >> 7;

    /*
    Device Init/Pairing message (submessage not fully decoded). Emitted by any device class on
    power-up, and *periodically* by the soil sensor as a sender-only re-announce (the sensor
    never receives) - which is how a power-cycled Water Timer re-pairs without a back-channel.
    target_id varies: a power-up INIT is a broadcast (0, as the Water Timer's here); the soil
    sensor's periodic re-announce is addressed to the paired gateway (target_id = gateway id).
    Layout: TY ff CC CC 00 LL FW [SS]
      TY device_type (byte[0]); CC device class byte x2; FW firmware (byte[6] = b[17]);
      soil sensor is length 8 with a trailing SS = session/pairing nibble that varies between
      broadcasts (seen 0x03 and 0x07), the Water Timer is length 7 with no SS.
    msg_counter (b[8]) is a 6-bit counter (1..63, wraps 63->1, 0 reserved): it resets to 1 on
    a real power-up (so the Water Timer's INIT reads 1) but runs continuously through the soil
    sensor's periodic re-announce - so the gap between two soil INIT counters is its period.
    */
    if (msg_type == 0x01 && (msg_length == 0x07 || msg_length == 0x08)) {

        int device_type = b[11]; // 0x0e soil, 0x06 water timer, 0x05 on the PR author's unit; same field as 0x09
        int firmware    = b[17];
        char msg[17];
        int n = 0;
        for (int i = 0; i < msg_length && n < (int)sizeof(msg) - 2; i++)
            n += snprintf(msg + n, sizeof(msg) - n, "%02x", b[11 + i]);

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture" "Bresser-WaterTimer"
                "status",        "",            DATA_STRING, "Init Pairing",
                "id",            "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",     "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",   "Msg Counter", DATA_INT,    counter,
                "device_type",   "",            DATA_FORMAT, "%u", DATA_INT, device_type,
                "firmware",      "Firmware",    DATA_FORMAT, "%u", DATA_INT, firmware,
                "msg_type",      "",            DATA_FORMAT, "%X", DATA_INT, msg_type & 0xf,
                "msg_length",    "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "msg",           "",            DATA_STRING, msg,
                "mic",           "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    // Basestation Acknowledgement for Soil Moisture init message
    else if (msg_type == 0x81 && msg_length == 0x10) {

        /*
        Acknowledgement but message answer not yet decoded, not always same values, could be date and time information ?
        11 12 13 14 15 16 17 18 19 20 21

        00 05 05 e0 01 5a 9a e8 11 06 02
        00 05 05 e0 01 b9 46 ed 11 01 02
        00 05 05 e0 01 2d 48 ed 11 01 02
        00 05 05 e0 01 6c 48 ed 11 01 02
        00 05 05 e0 01 3b 4c ed 11 01 02
        */

        char msg[23];
        snprintf(msg, 23, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                 b[11],b[12],b[13],b[14],b[15],b[16],b[17],b[18],b[19],b[20],b[21]);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "status",          "",            DATA_STRING, "Pairing Acknowledgement",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%X", DATA_INT, msg_type & 0xf,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "msg",             "",            DATA_STRING, msg,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // if message from Soil Moisture Sensor message to Basestation?
    else if (msg_type == 0x03 && msg_length == 0x07) {

        int sensor_number = b[11];
        int battery_low   = (b[12] & 0x10) >> 4;
        int battery_level = (b[12] & 0x0f);
        int moisture      = b[14];   // b[13]=0x88 and b[15]=0x85 are field markers, not data
        int temperature_f = (b[17] << 8) | b[16];

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture"
                "id",            "",              DATA_FORMAT, "%u",   DATA_INT,    source_id,
                "sensor_number", "",              DATA_FORMAT, "%u",   DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",   DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "temperature_F", "Temperature",   DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature_f * 0.1f,
                "moisture",      "Moisture",      DATA_FORMAT, "%u %%",DATA_INT,    moisture,
                "battery_ok",    "Battery OK",    DATA_FORMAT, "%u",   DATA_INT,    !battery_low,
                "battery_level", "Battery Level", DATA_INT, battery_level,
                "mic",           "Integrity",     DATA_STRING,    "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    /*
    Acknowledgement (empty 1-byte payload). An ack's type is the acked type with bit 7 set:
    0x8a acks the Water Timer's 0x0a (gateway->valve), 0x89 the soil 0x09, 0x83 the 0x03. The low
    nibble of msg_type distinguishes them; the model is the sender's, from its id class byte.
    */
    else if ((msg_type == 0x83 || msg_type == 0x89 || msg_type == 0x8a) && msg_length == 0x01) {

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture" "Bresser-WaterTimer" "Bresser-Gateway"
                "status",          "",            DATA_STRING, "Acknowledgement",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%X", DATA_INT, msg_type & 0xf,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Water Timer -> Base station: relays the Soil Moisture Sensor reading. Same
    battery/moisture/temperature block as the sensor's own 0x09.
    b[11] and b[12] differ between device sets (0x06/0x06 here, 0x0b/0x05 in the
    header example above), so they are ids, not constants: b[11] the Water Timer
    device type, b[12] the paired sensor address (matches the sensor's own 0x09).
    b[13] varies over time (meaning unknown, emitted as "unknown"); the original decoder
    glued b[12]+b[13] into one misleading value. b[15]=0x88 and b[17]=0x85 are constant field
    markers (they precede moisture and temperature), not data.
    */
    else if (msg_type == 0x0a && msg_length == 0x09) {

        int device_type   = b[11];
        int sensor_number = b[12];
        int unknown       = b[13];   // varies over time, meaning not established
        int battery_low   = (b[14] & 0x10) >> 4;
        int battery_level = (b[14] & 0x0f);
        int moisture      = b[16];
        int temperature_f = (b[19] << 8) | b[18];

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-WaterTimer"
                "id",            "",              DATA_FORMAT, "%u",     DATA_INT,    source_id,
                "device_type",   "",              DATA_FORMAT, "%u",     DATA_INT,    device_type,
                "sensor_number", "",              DATA_FORMAT, "%u",     DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",     DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "temperature_F", "Temperature",   DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature_f * 0.1f,
                "moisture",      "Moisture",      DATA_FORMAT, "%u %%",  DATA_INT,    moisture,
                "unknown",       "Unknown",       DATA_FORMAT, "%02x",   DATA_INT,    unknown,
                "battery_ok",    "Battery OK",    DATA_FORMAT, "%u",     DATA_INT,    !battery_low,
                "battery_level", "Battery Level", DATA_INT,    battery_level,
                "mic",           "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    /*
    Soil Moisture Sensor -> Water Timer telemetry (captured 2026-07 with an SDR next to
    the sensor). Same battery/moisture/temperature block as the Water Timer 0x0a relay;
    this is the original reading before the Water Timer forwards it to the base station.
    Sub message layout: TY AD 00 BB 88 HH 85 TL TH
      TY device type (0x0e on HCS005FRF, 0x05 on other units), AD device address,
      BB battery, 0x88 moisture marker, HH moisture %, 0x85 temp marker, T temperature_F LE.
    */
    else if (msg_type == 0x09 && msg_length == 0x09) {

        int device_type   = b[11];
        int sensor_number = b[12];
        int battery_low   = (b[14] & 0x10) >> 4;
        int battery_level = (b[14] & 0x0f);
        int moisture      = b[16];
        int temperature_f = (b[19] << 8) | b[18];

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture"
                "id",            "",              DATA_FORMAT, "%u",     DATA_INT,    source_id,
                "device_type",   "",              DATA_FORMAT, "%u",     DATA_INT,    device_type,
                "sensor_number", "",              DATA_FORMAT, "%u",     DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",     DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "temperature_F", "Temperature",   DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature_f * 0.1f,
                "moisture",      "Moisture",      DATA_FORMAT, "%u %%",  DATA_INT,    moisture,
                "battery_ok",    "Battery OK",    DATA_FORMAT, "%u",     DATA_INT,    !battery_low,
                "battery_level", "Battery Level", DATA_INT,    battery_level,
                "mic",           "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    /*
    Water Timer watering event (14-byte submessage), sent when a scheduled or manual
    run actually waters. Fields verified against 30 events cross-checked with the
    HomGar app's own water-usage export, plus controlled manual runs from the app:
      b[14:16] programme id (tracks the schedule slot: one value per daily slot),
      b[16:18] scheduled-slot counter (LE, monotonic; advances per scheduled slot, not
               on manual runs),
      b[18]    trigger: high nibble = source (0x4 scheduled, 0x2 manual), low nibble =
               mode (0x1 normal, 0x2 misting) -> 0x41 sched-normal, 0x21 manual-normal,
               0x22 manual-misting,
      b[19]    unknown (small, differs normal vs misting; possibly a segment/pulse count),
      b[23]    actual run duration in seconds (a hand-stopped run reads the real elapsed
               time, not the programmed value; 0x3c=60 matches the app default of 1 min).
    */
    else if (msg_type == 0x04 && msg_length == 0x0e) {

        int sensor_number = b[11];
        int programme     = (b[14] << 8) | b[15]; // opaque slot id; big-endian on purpose so its %04x hex matches the on-wire byte order (unlike the numeric LE fields below)
        int cycle_counter = b[16] | (b[17] << 8);
        int trigger       = b[18];
        int unknown       = b[19];   // small; differs normal vs misting, possibly a segment/pulse count
        int duration_s    = b[23];

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-WaterTimer"
                "status",        "",              DATA_STRING, "Watering",
                "id",            "",              DATA_FORMAT, "%u",   DATA_INT,    source_id,
                "sensor_number", "",              DATA_FORMAT, "%u",   DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",   DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "programme",     "",              DATA_FORMAT, "%04x", DATA_INT,    programme,
                "cycle_counter", "",              DATA_INT,    cycle_counter,
                "trigger",       "",              DATA_FORMAT, "%02x", DATA_INT,    trigger,
                "unknown",       "Unknown",       DATA_FORMAT, "%02x", DATA_INT,    unknown,
                "duration_s",    "Duration",      DATA_FORMAT, "%u s", DATA_INT,    duration_s,
                "mic",           "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Base Station schedule/duration config (0x85), pushed to the Water Timer on each
    settings change in the app. Byte offsets decoded by setting distinctive values and
    diffing the payload:
      b[12:14] default run duration, LE u16 seconds (0x0078=120=2min, 0x495c=18780=5h13m),
      b[14:16] misting run-time, LE u16 seconds,
      b[16:18] misting interval, LE u16 seconds (0x0327=807=13m27s),
      b[18]    Soil Moisture Sensor address (0 = none),
      b[19]    Stop Plan moisture threshold, percent (skip a plan while the soil is wetter),
      b[24]    flow rate calibration, percent.
    */
    else if (msg_type == 0x85 && msg_length == 0x0f) {

        int default_duration_s = b[12] | (b[13] << 8);
        int mist_run_s      = b[14] | (b[15] << 8);
        int mist_interval_s = b[16] | (b[17] << 8);
        int sensor_number   = b[18];
        int stop_moisture   = b[19];
        int unknown         = b[22];   // 0x42 observed, meaning unknown
        int flow_rate       = b[24];

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "status",          "",              DATA_STRING, "Schedule config",
                "id",              "",              DATA_FORMAT, "%u",   DATA_INT,    source_id,
                "target_id",       "",              DATA_FORMAT, "%u",   DATA_INT,    target_id,
                "sensor_number",   "",              DATA_FORMAT, "%u",   DATA_INT,    sensor_number,
                "msg_counter",     "Msg Counter",   DATA_INT,    counter,
                "default_duration_s", "Default Duration", DATA_FORMAT, "%u s", DATA_INT, default_duration_s,
                "mist_run_s",      "Mist Run",      DATA_FORMAT, "%u s", DATA_INT,    mist_run_s,
                "mist_interval_s", "Mist Interval", DATA_FORMAT, "%u s", DATA_INT,    mist_interval_s,
                "stop_moisture",   "Stop Moisture", DATA_FORMAT, "%u %%", DATA_INT,   stop_moisture,
                "flow_rate",       "Flow Rate",     DATA_FORMAT, "%u %%", DATA_INT,   flow_rate,
                "unknown",         "Unknown",       DATA_FORMAT, "%02x",  DATA_INT,   unknown,
                "mic",             "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // Config-change counter (0x20): a one-byte counter that increments on each settings edit.
    else if (msg_type == 0x20 && msg_length == 0x02) {

        /* clang-format off */
        data_t *data = data_make(
                "model",          "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "status",         "",            DATA_STRING, "Config change",
                "id",             "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",      "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",    "Msg Counter", DATA_INT,    counter,
                "config_counter", "",            DATA_INT,    b[11],
                "mic",            "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Base Station schedule table (0x86): a 1-byte header (b[11]) then one 7-byte record per
    watering plan, emitted as a `plans` array. b[11] is a "more parts" flag - 1 = another 0x86
    page follows, 0 = this is the last page - CONFIRMED by pushing a 5-plan table that split
    2+2+1 across three frames with headers 1,1,0. A table with >2 plans splits across several
    0x86 messages (a 33-byte frame holds at most 2 records); reassemble the pages in msg_counter
    order until more_parts=0. Byte offsets (per record r) decoded by editing plans in the app:
      r[0]   enabled (0x80) | weekday bitmask (bits 0-6 = Sun..Sat, used in weekly mode),
      r[1]   minute | ((hour & 3) << 6),
      r[2]   type | (day_mode << 3) | (hour >> 2); type bit6=0x40 normal / bit7=0x80 misting;
             day_mode 1=every day, 2=odd, 3=even, 4=weekly,
      r[3:5] duration, LE u16 seconds,
      r[5:7] water usage limit, LE u16, in units of 0.1 L (0 = no limit).
    A misting plan's run-time and interval are not here; they ride in the 0x85 config.
    NOTE: `plan` is the record's 1-based index WITHIN THIS MESSAGE. A large table is split
    across several 0x86 messages with no global index in the frame, so it is not the app's
    absolute plan number; identify a plan by its start time.
    */
    else if (msg_type == 0x86 && msg_length >= 0x08) {

        char const *const day_mode[] = {"unknown", "every day", "odd days",
                "even days", "weekly", "unknown", "unknown", "unknown"};
        data_t *plan_data[4] = {0};
        int np      = 0;
        int n_plans = msg_length / 7;
        // Each record is 7 bytes after the 1-byte header; bound the reads (r[6] is the highest
        // byte touched) to the extracted 33-byte frame so a bogus msg_length cannot over-read.
        for (int p = 0; p < n_plans
                && np < (int)(sizeof(plan_data) / sizeof(plan_data[0]))
                && (12 + p * 7 + 6) < (int)sizeof(b); p++) {
            uint8_t const *r = &b[12 + p * 7];
            int enabled      = (r[0] & 0x80) ? 1 : 0;
            int weekday_mask = r[0] & 0x7f;
            int minute       = r[1] & 0x3f;
            int hour         = ((r[2] & 0x07) << 2) | (r[1] >> 6);
            int mode         = (r[2] >> 3) & 0x07;
            int duration_s   = r[3] | (r[4] << 8);
            int water_dl     = r[5] | (r[6] << 8);

            /* clang-format off */
            plan_data[np] = data_make(
                    "plan",          "",            DATA_INT,    np + 1,
                    "enabled",       "",            DATA_INT,    enabled,
                    "irrigation",    "",            DATA_STRING, (r[2] & 0x80) ? "misting" : "normal",
                    "start_hour",    "",            DATA_INT,    hour,
                    "start_minute",  "",            DATA_INT,    minute,
                    "day_mode",      "",            DATA_STRING, day_mode[mode],
                    "weekday_mask",  "",            DATA_FORMAT, "%02x",   DATA_INT,    weekday_mask,
                    "duration_s",    "Duration",    DATA_FORMAT, "%u s",   DATA_INT,    duration_s,
                    "water_limit_l", "",            DATA_FORMAT, "%.1f L", DATA_DOUBLE, water_dl / 10.0,
                    NULL);
            /* clang-format on */
            np++;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",       "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "status",      "",            DATA_STRING, "Schedule",
                "id",          "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",   "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter", "Msg Counter", DATA_INT,    counter,
                "more_parts",  "",            DATA_INT,    b[11] ? 1 : 0,
                "plans",       "",            DATA_ARRAY,  data_array(np, DATA_DATA, plan_data),
                "mic",         "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // Recurring status/heartbeat (educated guess). Payload e.g. "01 09 01 3c 00 0a 00"; b[14] =
    // 0x3c = 60 is a fixed heartbeat interval, the rest is not understood (kept as raw `msg`).
    else if (msg_type == 0x21 && msg_length == 0x07) {

        char msg[15];
        snprintf(msg, sizeof(msg), "%02x%02x%02x%02x%02x%02x%02x",
                 b[11], b[12], b[13], b[14], b[15], b[16], b[17]);

        /* clang-format off */
        data_t *data = data_make(
                "model",                "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "status",               "",            DATA_STRING, "Heartbeat",
                "id",                   "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",            "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",          "Msg Counter", DATA_INT,    counter,
                "heartbeat_interval_s", "",            DATA_INT,    b[14],
                "msg",                  "",            DATA_STRING, msg,
                "mic",                  "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // Periodic beacon (educated guess). Ack bit set; payload is usually just 0x02 (0xa1) / 0x00
    // (0xa0), with a fuller payload momentarily around a config push (kept as raw `msg`).
    else if (msg_type == 0xa1 || msg_type == 0xa0) {

        char msg[41];
        int n = 0;
        for (int i = 0; i < msg_length && n < (int)sizeof(msg) - 2; i++)
            n += snprintf(msg + n, sizeof(msg) - n, "%02x", b[11 + i]);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "status",          "",            DATA_STRING, "Beacon",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg",             "",            DATA_STRING, msg,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Gateway's reply to the Water Timer's 0x02 status request (0x02|0x80, same counter). gateway_time
    is a monotonic u24 the base keeps cloud-synced (it reset to 0 on a base power-off and re-synced
    on boot); it behaves like a wall clock but its exact unit is unconfirmed, so it is emitted raw
    and the status is left generic rather than claiming "time".
    */
    else if (msg_type == 0x82 && msg_length >= 0x05) {

        uint32_t gateway_time = b[13] | (b[14] << 8) | (b[15] << 16);
        char msg[41];
        int n = 0;
        for (int i = 0; i < msg_length && n < (int)sizeof(msg) - 2; i++)
            n += snprintf(msg + n, sizeof(msg) - n, "%02x", b[11 + i]);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "status",       "",            DATA_STRING, "Status response",
                "id",           "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",    "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",  "Msg Counter", DATA_INT,    counter,
                "gateway_time", "",            DATA_INT,    (int)gateway_time,
                "msg",          "",            DATA_STRING, msg,
                "mic",          "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Water Timer -> gateway poll. The gateway answers each poll with reply type msg_type|0x80,
    echoing the counter (verified in every captured exchange, reply ~0.07 s later):
      0x02 -> 0x82  status request   (the poll carries the Water Timer's own state; see 0x82)
      0x05 -> 0x85  config request
      0x06 -> 0x86  schedule request  (byte[2], 0x00/0x01, appears to select a schedule page)
    The poll payloads are emitted raw (msg) but not yet field-decoded.
    */
    else if (msg_type == 0x02 || msg_type == 0x05 || msg_type == 0x06) {

        char const *status = (msg_type == 0x02) ? "Status request"
                           : (msg_type == 0x05) ? "Config request"
                           :                      "Schedule request";
        char msg[41];
        int n = 0;
        for (int i = 0; i < msg_length && n < (int)sizeof(msg) - 2; i++)
            n += snprintf(msg + n, sizeof(msg) - n, "%02x", b[11 + i]);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-WaterTimer"
                "status",       "",            DATA_STRING, status,
                "id",           "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",    "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",  "Msg Counter", DATA_INT,    counter,
                "msg_type",     "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "msg_length",   "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "msg",          "",            DATA_STRING, msg,
                "mic",          "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Not-yet-decoded control frames fall through here and are emitted with their raw `msg`.
    Still opaque, seen during a Water Timer re-pair (deliberately left undecoded, not guessed):
      0x81 len 11  base -> valve, e.g. "000506e00110c7e8190137" - was our date/time-sync
                   candidate; 0x82 turned out to be the poll reply instead, so 0x81's role is unknown.
    */
    else {

        // Not yet decoded message type - emit its raw sub-message (msg_length bytes) for analysis
        char msg[41];
        int n = 0;
        for (int i = 0; i < msg_length && n < (int)sizeof(msg) - 2; i++)
            n += snprintf(msg + n, sizeof(msg) - n, "%02x", b[11 + i]);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture" "Bresser-WaterTimer" "Bresser-Gateway" "Bresser-Garden"
                "status",          "",            DATA_STRING, "Unknown msg",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "msg",             "",            DATA_STRING, msg,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return 0;

}

static char const *const output_fields[] = {
        "model",
        "id",
        "device_type",
        "sensor_number",
        "station_id",
        "target_id",
        "msg_counter",
        "temperature_F",
        "status",
        "firmware",
        "moisture",
        "programme",
        "cycle_counter",
        "trigger",
        "duration_s", "default_duration_s",
        "mist_run_s", "mist_interval_s", "stop_moisture", "flow_rate",
        "config_counter",
        "plans",
        "plan", "enabled", "irrigation", "start_hour", "start_minute", "day_mode", "weekday_mask", "water_limit_l",
        "unknown", "heartbeat_interval_s",
        "battery_ok", "battery_level",
        "acknowledgement", "msg_type", "msg_length",
        "msg",
        "mic",
        NULL,
};

r_device const bresser_garden = {
        .name        = "Bresser SmartHome Garden 7510100/7510200 (Baldr Homgar, RainPoint)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 10000, // long part of the message could be zeros
        .decode_fn   = &bresser_garden_decode,
        .fields      = output_fields,
};
