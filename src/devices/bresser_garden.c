/** @file
    Bresser SmartHome Garden set.

    Copyright (C) 2024 Bruno OCTAU (\@ProfBoc75)
    Copyright (C) 2026 Mattias Jonsson (\@mjonss)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

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
    default: return "Bresser-Garden"; // fallback for an unrecognised device class
    }
}

// Fill buf with the sub-message payload (from byte 11) as lowercase hex, for the raw "msg" field.
// buf must hold 2*20 + 1 bytes and is always NUL-terminated - an empty payload yields "". The
// caller rejects msg_length above 20 (b[11] .. b[30], the last two of the 33 bytes being the CRC);
// the clamp here repeats that bound so the write stays inside buf however this is called.
static void print_payload_hex(char *buf, uint8_t const *b, int msg_length)
{
    int len = msg_length > 20 ? 20 : msg_length; // msg_length is a byte, so always >= 0
    char *p = buf;
    for (int i = 0; i < len; i++)
        p += sprintf(p, "%02x", b[11 + i]);
    *p = '\0';
}

/** @fn int bresser_garden_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Bresser SmartHome Garden set (Fujian Baldr / Homgar family, also sold as RainPoint). 433 MHz ISM
band, FSK PCM, bidirectional. Each frame is reported under the model of its actual transmitter,
taken from the top byte of the 32-bit id (the device class):

    model                  class  Bresser              Homgar (Baldr)     RainPoint
    ---------------------  -----  -------------------  -----------------  -----------------------
    Bresser-Gateway        0x01   HWS388WRF-V7 7510100 HWS388             TWG004WRF (Wi-Fi hub)
    Bresser-WaterTimer     0x1F   HTV103FRF 7910100    HTV103 (1-zone)    ITV0103W / TTV1013WRF
                                  HTV203FRF 7910101    HTV203 (2-zone)    TTV203WRF (2-zone)
    Bresser-SoilMoisture   0x47   HCS005FRF 7910102    HCS005             ICS0001W
    Bresser-Garden          --    (fallback for an unrecognised class byte)

The three devices:
- Soil Moisture Sensor (0x47) - a small battery sensor. It normally sends telemetry to its Water
  Timer to be relayed (0x09), but sends straight to the gateway (0x03) when the app's "Relay
  Communication" is off. Its telemetry is fire-and-forget - an ack does not drive retransmission -
  but it does RECEIVE: pairing, config and acknowledgement traffic all reach it on ~434.57 MHz.
- Water Timer / valve (0x1F) - the battery irrigation valve. It has no soil probe: it relays the
  soil sensor's reading to the gateway, and stores the watering schedule and runs it on its own
  clock, so it keeps watering even with the gateway off.
- Gateway / base station (0x01) - the mains-powered Wi-Fi hub with a display; bridges to the HomGar
  cloud / app. Pairing is set up in the app (the water timer's "Select Soil Sensor") and is partly
  visible on the RF - the 0x01 INIT and its 0x81 reply are seen during a re-pair.

FCC: gateway 2AWDBHWS388WRF, soil sensor 2AWDBTCS005FRF. Issue #2988 (\@kami83), PR #3621
(based on #3005).

Topology (the outdoor thermo-hygro unit is separate; see "Not handled" below):

        Soil Moisture Sensor (0x47)
                 |
                 |  0x09  moisture / temperature   relay mode (the default)
                 v
        Water Timer / valve (0x1F)
                 ^
                 |
      Water Timer -> gateway:  0x0a (relayed soil reading), 0x04 (watering), 0x02 (status), ...
      gateway -> Water Timer:  0x8a (ack), 0x85 / 0x86 (schedule config), 0x20 / 0x21 (control), ...
                 |
                 v
        Gateway / base station (0x01)  ==Wi-Fi==>  HomGar cloud / app

        Direct mode ("Relay Communication" off):
        Soil Moisture Sensor  --0x03-->  Gateway  --0x83-->  Soil Moisture Sensor

Not handled by this decoder:
- The outdoor thermo-hygro sensor (Homgar H666TH / H999TH) the base also reads: it uses the
  already-supported Bresser Thermo-/Hygro 3CH decoder (protocol 52), not this one.
- The base's barometric pressure: shown on the display / sent to the cloud only, never on 433 MHz.

Physical layer: 433 MHz ISM band, FSK PCM at 50 us/bit (20 kbit/s). FSK deviation runs ~36 kHz for
the soil sensor, ~38 kHz for the gateway, ~65 kHz for the Water Timer. Per-channel center frequencies
are in the "frequencies" section below.

Frame envelope (every message; after the aa..aa f3 e9 10 5e 51 preamble+sync the frame is always
33 bytes). The Soil Moisture Sensor prepends a long ~1250-bit wake-up preamble, but its frame is
still 33 bytes:

    bytes:   0  1  2  3   4  5  6  7   8   9  10   11 ...... sub-message ......    31 32
    field:   TT TT TT TT  SS SS SS SS  RR  MT  LL   MM MM MM ..................    ZZ ZZ

- TT:{32} Target id, little-endian - the specific paired peer's id (e.g. the soil sensor's frames
          carry its water timer's id), NOT a broadcast. Exception: the 0x01 INIT targets 0x00000000.
          Telemetry frames surface this recipient as the `station_id` field, control frames as
          `target_id` - the same value under two names, split by message class.
- SS:{32} Source id, little-endian, fixed per device. Top byte = device class (0x01/0x1F/0x47),
          pattern CC00xxxx (the 2nd byte has always been 0x00; the low 16 bits are a per-unit
          serial). Survives a power cycle (the 0x01 INIT after power-up carries the same id).
- RR: {8} Message counter. Only 1..63 are used - a 6-bit counter that wraps 63 -> 1 (0 reserved),
          seen on all three devices. A reply/ack echoes the request's value.
- MT: {8} Message type. Bit 7 (0x80) is the REPLY flag: reply = 0x80 | request (0x0a -> 0x8a,
          0x02 -> 0x82), echoing the counter. A reply is EITHER an empty acknowledgement (when the
          data was already in the request, e.g. telemetry) OR carries the requested data (when the
          request was a bare poll/fetch, e.g. 0x02 -> 0x82, 0x08 -> 0x88). Low 7 bits identify the
          sub-message (see the per-type sections below).
- LL: {8} Sub-message length (meaningful payload bytes; the rest of the 33-byte frame is zero-pad).
- MM:{..} Sub-message, starting at byte 11 (see below).
- ZZ:{16} CRC-16 (poly 0x1021, init 0xd636) over bytes 0..30, ALWAYS at bytes 31..32 - the frame is
          a fixed 33 bytes regardless of LL, so the CRC position never moves.

Data layout - four real frames from this set (soil sensor 0x470005B5, water timer 0x1F000D9C,
gateway 0x01000EC2), preamble + sync word stripped, ids little-endian on the wire. "?" = a byte not
decoded in that message's section.

    field:   TT TT TT TT  SS SS SS SS  RR  MT  LL   MM ...                        ZZ ZZ

  0x09  soil sensor -> water timer   (soil's own moisture/temp; one-way, relayed as 0x0a)
    9c 0d 00 1f  b5 05 00 47  0f  09  09   0e 06 00 08 88 2f 85 94 02  00..00  12 c5
    ->1F000D9C   470005B5     15         (b13 = 00 here; the water timer inserts soil_rssi in 0x0a)

  0x0a  water timer -> gateway       (relays that reading and adds its own soil_rssi; see "0x0a")
    c2 0e 00 01  9c 0d 00 1f  23  0a  09   06 06 17 08 88 2f 85 94 02  00..00  5a c0
    ->01000EC2   1F000D9C     35         soil_rssi=0x17 (23, ~-116 dBm), moisture=0x2f (47%), temp 66.0F

  0x8a  gateway -> water timer       (ack of the 0x0a: MT = 0x80|0x0a, echoes counter 35, empty)
    9c 0d 00 1f  c2 0e 00 01  23  8a  01   00                          00..00  39 d0
    ->1F000D9C   01000EC2     35

  0x04  water timer -> gateway       (watering event; see "0x04")
    c2 0e 00 01  9c 0d 00 1f  17  04  0e   06 01 01 ad ae e4 19 21 05 00 00 00 3c 00  d8 e0
    ->01000EC2   1F000D9C     23         trigger=0x21 (manual/normal), duration=0x3c (60 s)

Direction: in relay mode the soil sensor sends 0x09 to the Water Timer, which forwards the reading
as 0x0a; in direct mode it sends the compact 0x03 to the gateway and is acked with 0x83. An ack goes
to whoever SENT the data - above, the gateway acks the Water Timer, echoing its counter. Telemetry is
fire-and-forget either way: receiving an ack does not drive retransmission.

Each sub-message below starts at byte 11. Layouts are shown as "Layout: <bytes>", "?" = undecoded.

Message inventory (S = soil sensor, V = Water Timer/valve, G = gateway/base; "len" = payload length;
"name" is a descriptive label, not on-air data):

    type   len       dir           name              meaning
    -----  --------  ------------  ----------------  --------------------------------------------------
    0x01   7 or 8    any->bcast/G  Init / pairing    power-up announce; soil also re-announces
    0x02   15        V->G          Status poll       poll; also carries the live watering countdown
    0x03   7         S->G          Soil telemetry    direct soil moisture/temp (compact form of 0x09)
    0x04   14        V->G          Watering event    a watering run actually happened
    0x05   2         V->G          Config request    fetch the 0x85 schedule/duration config
    0x06   3         V->G          Schedule request  fetch an 0x86 schedule page (byte selects page)
    0x08   var       V->G          Moisture request  fetch current moisture
    0x09   9         S->V          Soil telemetry    relay soil moisture/temp to the valve
    0x0a   9         V->G          Relay telemetry   valve re-emits the soil block + its soil RSSI
    0x20   2 or 3    G->V          Config change     config-change counter (+ RF channel, 3-byte form)
    0x21   >=3       G->V          Run / keep-alive  start/stop a run, or a recurring keep-alive
    0x81   11 or 16  G->S/V        Pairing/control   len 16 replies to the soil INIT; len 11 unknown
    0x82   >=2       G->V          Status reply      reply to 0x02: config counter (+ gateway time)
    0x83/84/89/8a  1 any           Acknowledgement   empty ack (bit 7 set on the acked type)
    0x85   15        G->V          Schedule config   default/misting durations, sensor addr, threshold
    0x86   8 or 15   G->V          Schedule table    watering-plan table (paged), 1 or 2 records
    0x88   >=3       G->V          Moisture reply    reply to 0x08
    0xa0   -         V->G          Acknowledgement   valve acks the 0x20 config change
    0xa1   -         V->G          Run reply/beacon  reply to a 0x21 run; else a ~120 s beacon (02)

A reply's type is the request's type with bit 7 set (req | 0x80), echoing the request's counter; it
is either an empty ack (data already in the request, e.g. telemetry) or carries the fetched data
(0x02->0x82, 0x08->0x88). A couple of types (0x21, 0xa1) cover more than one behaviour, told apart by
their payload.

=== pairing / startup ===

0x01 - INIT / pairing announce (on power-up, and periodically re-announced)

  Layout (7 bytes Water Timer, 8 bytes soil sensor):  TY FF CC CC 00 ?? FW [SS]
  - TY:{8} device type (0x0e HCS005FRF soil, 0x06 Water Timer, 0x05 on other soil units),
  - FF:{8} 0xff,
  - CC CC:{16} the device class byte, duplicated on the reference (single-valve) unit (47 47 soil,
    1f 1f valve) - a 2-zone valve (HTV203FRF) might differ; untested,
  - FW:{8} firmware (0x35 = 53, matches the app),
  - SS:{8} soil sensor only (the 8-byte form): a session/pairing nibble that varies between announces.
  A genuine power-up broadcasts to 0x00000000 with the counter reset to 1; the soil sensor's periodic
  re-announce instead addresses its paired gateway. A power-up INIT can be provoked from the soil
  sensor's own buttons as well as by removing its batteries (which button, and whether re-pairing
  differs from a plain power cycle, is untested).

0x81 - pairing / control (two forms, both rare)

  The len-16 form is decoded as the Pairing ack, the gateway's reply to the soil 0x01 INIT; its
  payload is not further decoded, only emitted raw. A separate len-11 form was caught in a flex
  capture during a manual re-pair, addressed to the WATER TIMER rather than the soil
  (e.g. "000506e00110c7e8190137"); it does not match the len-16 guard, so it falls through to
  Unknown msg. Both payloads vary; roles unclear.

=== telemetry ===

0x09 - Soil Moisture Sensor -> Water Timer, the sensor's own reading

  Layout:  TY AD 00 BB 88 HH 85 TL TH
  - TY:{8} device type (0x0e HCS005FRF, 0x05 on other units),
  - AD:{8} the soil sensor's device address (0x06 on this set),
  - 00:{8} constant (this is where the Water Timer inserts soil_rssi in its 0x0a relay),
  - BB:{8} battery (0x09 full / 0x11 low; low nibble = level, high nibble = low-battery flag),
  - 88:{8} moisture field marker, HH:{8} moisture %,
  - 85:{8} temperature field marker, TL TH:{16} temperature, signed little-endian, Fahrenheit x10.

0x0a - Water Timer -> gateway, RELAYED soil reading

  The Water Timer has no soil probe; this relays the soil sensor's 0x09 (same battery/moisture/
  temperature block) and adds soil_rssi.
  Layout:  ID AD SR BB 88 HH 85 TL TH
  - ID:{8} Water Timer device type (0x06 on HTV103FRF),
  - AD:{8} the paired soil sensor's address (0x06; matches the 0x09),
  - SR:{8} soil_rssi - the raw RSSI register the Water Timer reports for the soil sensor (the value
           the app shows as the soil-sensor RSSI; ~= 0.36*SR - 124.7 dBm). Emitted raw. Absent from
           the sensor's own 0x09 (constant 00 there); only the relay adds it.
  - BB, 88, HH, 85, TL TH: as in 0x09.

0x03 - direct Soil -> base telemetry

  Emitted when the app's "Relay Communication" is turned off: the sensor then talks straight to the
  gateway instead of via the Water Timer. Same battery/moisture/temperature block as 0x0a, but the
  compact form - it drops the sensor address and the spacer byte, so the markers land two bytes
  earlier. Note the sensor also CHANGES FREQUENCY with the mode (see the frequencies section).

0x83 / 0x84 / 0x89 / 0x8a - acknowledgements (empty payload, length 1)

  An ack's type is the acked type with bit 7 set, and it echoes the acked message's counter (RR):
  0x8a acks the Water Timer's 0x0a, 0x89 the soil 0x09, 0x83 the 0x03, 0x84 the 0x04 watering event.

=== watering ===

0x04 - Water Timer -> gateway, watering event

  Emitted when a scheduled or manual run actually delivers water. A scheduled slot is SKIPPED (no
  0x04) when the soil is already wet enough: the Water Timer reads the moisture from the gateway
  (0x08 below) and compares it against the Stop-Plan threshold in the 0x85 config. So 0x04 appears
  as often as the watering schedule fires, minus the skipped-because-wet slots.
  Layout:  AD ?? ?? PR PR CC CC TR WW WW 00 00 DR DR
  - AD:{8} the associated soil sensor's address (which sensor this plan uses; 0x06 here),
  - PR PR:{16} programme id, big-endian, tracks the schedule slot (one value per slot),
  - CC CC:{16} counter, little-endian, monotonic over days and NOT bumped by manual runs. It does
    not advance once per run either - repeated scheduled runs of one plan in a night all read the
    same value - so the granularity is coarser (per day?); unconfirmed,
  - TR:{8} trigger: high nibble = source (0x4 scheduled, 0x2 manual from the app, 0x1 the button on
    the Water Timer itself), low nibble = mode (0x1 normal, 0x2 misting) - e.g. 0x41 scheduled-normal,
    0x21 manual-normal, 0x22 manual-misting, 0x42 scheduled-misting, 0x11 button-normal. A button
    press runs for default_duration_s from the 0x85 config,
  - WW WW:{16} water usage of this run, little-endian, units of 0.1 L. The Water Timer MEASURES it
    with a flow meter - a run with the supply shut off reports 0 - so it is not run time times a
    constant. The same figure is echoed by the following 0x02 and 0xa1 frames (see 0x02). b[21:23]
    are always zero, so the field may be wider than 16 bits.
    The meter is rated 5-35 L/min and reads true within that band, but under-reads below it (27 % low
    at 1.26 L/min against a measured volume). Drip irrigation runs an order of magnitude below the
    range, so its litres under-report and the real flow is higher than reported. The 0x85 flow_rate
    is a single trim and can only be correct at one flow rate,
  - DR DR:{16} duration, the run's elapsed length in seconds, little-endian. WALL-CLOCK: it counts
    the misting pauses and is padded to the programmed total, so a 360 s misting plan that delivered
    only 15 s of water still reports 360, matching the 0x02 duration_s for the same run. It is
    measured rather than copied from config, so a run stopped early reports less.

=== base -> Water Timer control and config ===

The base pushes control and config to the Water Timer; a config change normally triggers the Water
Timer's fetch rounds (0x05/0x06, below).

0x20 - config-change counter (and RF channel change)

  b[11] is a counter that bumps on each settings edit (0x1e -> 0x1f). The base emits it around a
  config push; the Water Timer then acks (0xa0) and fetches with 0x05/0x06 (below).
  A 3-byte variant also reports the changed setting: b[12] = field id (0x04 = RF communication
  channel), b[13] = the new value (Channel 2 -> "..0402", Channel 3 -> "..0403"), emitted
  as rf_channel. The channel moves ONLY the valve->gateway uplink (center ~433.17 Ch1, ~434.68 Ch2,
  ~433.24 Ch3); the downlink and everything to the soil stay fixed. See the "frequencies" section.

0x21 - base -> Water Timer run command / heartbeat

  Layout:  01 VV MM DD DD ...   VV = variant (0x02 manual run, 0x09 recurring keep-alive), MM = mode
  (0 stop, 1 normal, 2 misting), DD DD = duration s, little-endian (16 bits: a 300 s run sends
  "01 02 01 2c 01").
  Drives a manual run: "01 02 02 78 00" starts a 120 s misting run (VV=02, MM=02, DD=0x78), "01 02 00"
  stops it (VV=02, MM=00), each answered by an 0xa1 with the run state. The recurring "01 09 01 3c 00
  0a" variant (VV=09, DD=0x3c=60 s) is emitted as a Heartbeat; its exact role is unconfirmed.
  Gateway-dependent.

0x85 - schedule / duration config (base -> Water Timer; also the 0x05 fetch reply)

  Pushed on each app settings change.
  Layout:  00 DD DD MR MR MI MI AD ST 00 00 ?? 00 FR ??   (15 bytes; b[25], the last, is undecoded)
  - DD DD:{16} default run duration, little-endian seconds (0x0078=120=2min, 0x495c=18780=5h13m),
  - MR MR:{16} misting run-time, little-endian seconds,
  - MI MI:{16} misting interval, little-endian seconds,
  - AD:{8} selected soil sensor address (0 = none; always 0x06 on this set),
  - ST:{8} the app's "Stop Plan Moisture" % - a scheduled plan is skipped while the soil is wetter
    than this (it tracks the app value, e.g. 62 -> 46). This is what makes 0x04 watering
    events rarer than the schedule.
  - ??:{8} (b[22]) unknown, 0x42 observed.
  - FR:{8} flow_rate, SIGNED (int8): the app's -20..+20 % calibration (-20 -> 0xec). It is a trim on
    the water usage the Water Timer measures (see 0x04), not the basis of a computed figure.
    Being one constant it can only be correct at one flow rate.
  The misting run-time / interval are here (global), NOT per-plan in 0x86 - so ALL misting plans and
  manual misting runs share the one pattern (verified from the config log: changing misting for any
  one schedule moves this single global pair; default_duration is separate and unaffected).

0x86 - schedule table (base -> Water Timer; also the 0x06 fetch reply)

  A 1-byte header then one 7-byte record per plan. Header b[0] = more_parts flag (1 = another 0x86
  page follows, 0 = last; a table with >2 plans splits across pages, reassembled in counter order).
  Layout per record:  WD MN TY DD DD WL WL
  - WD:{8} enabled (0x80) | weekday bitmask (bits 0..6 = Sun..Sat, used in weekly mode),
  - MN:{8} minute | ((hour & 3) << 6),
  - TY:{8} irrigation-type | (day_mode << 3) | (hour >> 2): type bit6 (0x40) normal / bit7 (0x80)
    misting; day_mode 1 every day, 2 odd, 3 even, 4 weekly,
  - DD DD:{16} duration, little-endian seconds,
  - WL WL:{16} water-usage limit, little-endian, units of 0.1 L (0 = no limit). The Water Timer
    enforces it ITSELF, cutting a plan short once the metered volume reaches the cap - no stop
    command comes from the gateway, in keeping with it running schedules on its own clock. The cap
    is on METERED litres, so below the meter's rated range (see 0x04) it is reached later than the
    true volume warrants and more water goes out than the setting implies.

0xa0 / 0xa1 - the Water Timer's replies to the base's control frames

  - 0xa0 = ack of the 0x20 config change (0x80 | 0x20): empty payload, echoes the counter.
  - 0xa1 = reply to the 0x21 run command (0x80 | 0x21): carries the resulting run state (trigger,
    remaining_s, duration_s) in the same 0x9f/0x81/0xad marker layout as the 0x02 report. Its byte
    after the 0x9f (b[14:16]) is the last run's water usage, exactly as in 0x02 - both messages
    share the sub-structure GG 9f WW WW .. 81 RR RR ad DD DD (placeholders as in 0x02 below:
    GG trigger, WW WW water usage, RR RR remaining_s, DD DD duration_s).
  A frequent (~120 s) bare "02" 0xa1 also appears whose role is unproven - it is labelled Beacon.
  All are gateway-dependent (they vanish with the base powered off).

=== Water Timer polls, and the gateway's replies ===

The Water Timer periodically reports its status and fetches config/moisture (the fetches are
normally triggered by the base's 0x20 config-change above); the gateway answers with type|0x80
(echoing the counter, ~0.07 s later).

0x02 - status report (len 15). The Water Timer pushes its own run state; the gateway replies 0x82.

  Layout:  06 TT 01 GG 9f WW WW 00 00 81 RR RR ad DD DD
  - GG:{8} trigger, same encoding as the 0x04 event; 0 when idle,
  - WW WW:{16} water usage of the last COMPLETED run, units of 0.1 L - the same field the 0x04 event
    carries, held until the next run ends. So a status report pairs the current run's countdown with
    the PREVIOUS run's usage; it never shows the run in progress,
  - RR RR:{16} remaining_s, little-endian - seconds left in the current run, counting down 1/s and
    continuing through misting pauses,
  - DD DD:{16} duration_s, little-endian - the run's total length.
  trigger, remaining_s and duration_s read 0 when idle, so a 0x02 doubles as a live watering
  countdown; water usage persists. It is sent on a fixed ~9.4 min timer whether or not the gateway
  answers. 0x9f/0x81/0xad are constant markers. TT (b[12]) is undecoded - it varies within a single
  run (0x0b at the start, 0x0e later).

0x05 / 0x06 / 0x08 - fetches (bare requests; the data comes back in the reply):
  - 0x05 get-config   -> 0x85 reply (the schedule/duration config),
  - 0x06 get-schedule -> 0x86 reply (byte[2] selects a page for multi-page tables),
  - 0x08 get-soil-moisture (payload "06 06" = soil address 6, then a constant) -> 0x88 reply with the
    current moisture %. The Water Timer relays the soil reading itself yet still asks the gateway for
    it - probably to act on the gateway's (cloud-authoritative) value; unresolved.
  These are event-driven: every 0x05/0x06 burst follows a 0x20 config-change notice (above).

0x82 - reply to the 0x02 status report (same counter)

  Two forms: a bare 2-byte ack, or a longer form (>= 5 bytes) carrying gateway_time = b[13:16]
  (little-endian u24), a seconds-scale monotonic counter (~1.07 ticks/s free-run, measured over 111
  samples; cloud-disciplined, so it takes occasional large forward jumps). It is NOT a disciplined
  wall clock - absolute time can't be read from it - and being u24 it wraps roughly every ~180 days.
  It is emitted raw and the status is the generic "Status response". payload[1] (b[12]) is the
  gateway's config_counter (the 0x20 value), which is how a config change reaches the valve.

0x88 - reply to the 0x08 moisture fetch:  00 00 MM,  MM = moisture %.

=== autonomy ===

The Water Timer runs the schedule on its own clock with the base absent (it stores the 0x86 table
and emits 0x04 events); the base is only the cloud gateway, config pusher and time (0x82) source. A
disabled plan rides in the 0x86 table with enabled=0 rather than being dropped.

=== frequencies ===

Per-channel FSK center frequencies ((freq1+freq2)/2), measured 2026-07-23 and cross-checked on two
SDRs (absolute values carry ~+-40 kHz, but the relationships are solid). The RF Communication Channel
moves ONLY the valve->gateway uplink; the downlink, the soil->valve relay, and everything to the soil
(~434.57) are channel-independent.

    link                              Ch1       Ch2       Ch3      behaviour
    --------------------------------  --------  --------  -------  ----------------------------
    valve -> gateway uplink           ~433.17   ~434.68   ~433.24  moves (Ch2 far up; Ch1 ~ Ch3)
    gateway -> valve downlink         ~433.69   ~433.70   ~433.69  fixed
    soil 0x09 -> valve (relay)        ~433.68   ~433.70   ~433.69  fixed
    to-soil (0x89 / 0x83 / config)    434.57    434.57    434.57   fixed

The soil sensor CHANGES TRANSMIT FREQUENCY WITH ITS MODE, which is why direct-mode frames are easy
to miss: on Channel 3 its direct 0x03 -> gateway was measured ~433.34, while its relayed 0x09 -> Water
Timer uses the fixed ~433.69 link, and the 0x01 INIT broadcast sweeps ~433.1 + 433.3 on boot/re-pair.
Everything sent TO the soil stays on its fixed ~434.57 receive channel. The values above are FSK
centers ((f1+f2)/2), not single tones.

=== known unknowns / guesses ===

- 0xa1 Beacon (payload 02, ~120 s): role unexplained; it also arrives ~7 dB hotter than every other
  frame, unexplained.
- 0x81 pairing-ack payload, the 0x21 "01 09.." keep-alive role, 0x85 bytes b[22] (0x42 constant) and
  b[25], 0x04 bytes b[12]/b[13], the 0x01 INIT b[16], and the 0x82 long form's trailing "19 06"
  (after config_counter and the u24 gateway_time): all undecoded.
- 0x02 byte b[12] (TT) changes WITHIN a single run, so it tracks live state rather than config -
  probably the most tractable of the remaining unknowns.
- Water-usage field width: read as 16-bit LE (0x04 b[19:21], 0x02 b[16:18], 0xa1 b[14:16]). The two
  bytes above it are zero in every capture, so it could be 24 or 32 bits; only a run past 6553.5 L
  would tell, which is not a practical experiment.
- soil_rssi -> dBm (0x0a) is an empirical linear fit, unconfirmed above register ~150.
- bcfa4417.. frames: ~1520-bit frames with a DIFFERENT sync word (not f3 e9 10 5e 51); not part of
  this frame format - likely a separate gateway link, possibly pairing.
- Multi-sensor topology: the set reportedly supports several soil sensors; untested (one here). Each
  0x0a relay carries the soil sensor's own address, so several sensors would interleave by address.

*/

static int bresser_garden_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xf3, 0xe9, 0x10, 0x5e, 0x51};

    uint8_t b[33];

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 1, __func__, "Expected one row: %d", bitbuffer->num_rows);
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

    if (crc16(b, 33, 0x1021, 0xd636)) {
        decoder_logf(decoder, 1, __func__, "CRC error");
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 1, __func__, b, 33 * 8, "MSG");

    // Extract info ...

    uint32_t target_id  = ((uint32_t)b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0];
    uint32_t source_id  = ((uint32_t)b[7] << 24) | (b[6] << 16) | (b[5] << 8) | b[4];
    int counter         = b[8];
    int msg_type        = b[9];
    int msg_length      = b[10];
    int acknowledgement = msg_type >> 7; // bit 7 is the reply/ack flag

    // The payload runs b[11]..b[30] - the last two of the fixed 33 bytes are the CRC - so a declared
    // length above 20 cannot be real, whatever the CRC says. Reject rather than decode past the end.
    if (msg_length > 20) {
        decoder_logf(decoder, 1, __func__, "Invalid payload length: %d", msg_length);
        return DECODE_FAIL_SANITY;
    }

    // 0x01 Init/Pairing (see header): device_type b[11], firmware b[17]; payload is otherwise
    // emitted raw as msg (not fully decoded).
    if (msg_type == 0x01 && (msg_length == 0x07 || msg_length == 0x08)) {

        int device_type = b[11]; // 0x0e soil, 0x06 water timer; same field as 0x09
        int firmware    = b[17];
        char msg[41];
        print_payload_hex(msg, b, msg_length);

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture" "Bresser-WaterTimer"
                "msg_name",      "",            DATA_STRING, "Init Pairing",
                "id",            "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",     "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",   "Msg Counter", DATA_INT,    counter,
                "device_type",   "",            DATA_FORMAT, "%u", DATA_INT, device_type,
                "firmware",      "Firmware",    DATA_FORMAT, "%u", DATA_INT, firmware,
                "msg_type",      "",            DATA_FORMAT, "%X", DATA_INT, msg_type,
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

        // Acknowledgement of the INIT; payload varies between captures and is not decoded.
        char msg[41];
        print_payload_hex(msg, b, msg_length);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "msg_name",        "",            DATA_STRING, "Pairing ack",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%X", DATA_INT, msg_type,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "msg",             "",            DATA_STRING, msg,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // 0x03 direct soil -> base telemetry, sent when "Relay Communication" is off (see header 0x03)
    else if (msg_type == 0x03 && msg_length == 0x07) {

        int device_type   = b[11];
        int battery_low   = (b[12] & 0x10) >> 4;
        int battery_level = (b[12] & 0x0f);
        int moisture      = b[14]; // b[13]=0x88 and b[15]=0x85 are field markers, not data
        int temperature_f = (int16_t)((b[17] << 8) | b[16]); // signed: below-zero F is two's complement

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture"
                "msg_name",      "",              DATA_STRING, "Soil telemetry",
                "id",            "",              DATA_FORMAT, "%u",   DATA_INT,    source_id,
                "device_type",   "",              DATA_FORMAT, "%u",   DATA_INT,    device_type,
                "station_id",    "",              DATA_FORMAT, "%u",   DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "msg_type",      "",              DATA_FORMAT, "%02X", DATA_INT,    msg_type,
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
    0x8a acks the Water Timer's 0x0a, 0x89 the soil sensor's 0x09, and 0x83 its 0x03. The full
    msg_type identifies the acked request; the model is the sender's, from its id class byte.
    */
    else if ((msg_type == 0x83 || msg_type == 0x84 || msg_type == 0x89 || msg_type == 0x8a) && msg_length == 0x01) {

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture" "Bresser-WaterTimer" "Bresser-Gateway"
                "msg_name",        "",            DATA_STRING, "Acknowledgement",
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg_type",        "",            DATA_FORMAT, "%X", DATA_INT, msg_type,
                "msg_length",      "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    0x0a Water Timer -> gateway: relays the soil sensor's 0x09 block and inserts soil_rssi at b[13]
    (raw RSSI register the app shows for the soil sensor; ~ 0.36 * soil_rssi - 124.7 dBm, emitted raw
    as the fit is empirical, unconfirmed above ~150). b[15]=0x88 and b[17]=0x85 are field markers.
    See header 0x0a.
    */
    else if (msg_type == 0x0a && msg_length == 0x09) {

        int device_type   = b[11];
        int sensor_number = b[12];
        int soil_rssi     = b[13]; // raw RSSI register the Water Timer reports for the soil sensor
        int battery_low   = (b[14] & 0x10) >> 4;
        int battery_level = (b[14] & 0x0f);
        int moisture      = b[16];
        int temperature_f = (int16_t)((b[19] << 8) | b[18]); // signed: below-zero F is two's complement

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-WaterTimer"
                "msg_name",      "",              DATA_STRING, "Relay telemetry",
                "id",            "",              DATA_FORMAT, "%u",     DATA_INT,    source_id,
                "device_type",   "",              DATA_FORMAT, "%u",     DATA_INT,    device_type,
                "sensor_number", "",              DATA_FORMAT, "%u",     DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",     DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "msg_type",      "",              DATA_FORMAT, "%02X",   DATA_INT,    msg_type,
                "temperature_F", "Temperature",   DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature_f * 0.1f,
                "moisture",      "Moisture",      DATA_FORMAT, "%u %%",  DATA_INT,    moisture,
                "soil_rssi",     "Soil RSSI",     DATA_INT,    soil_rssi,
                "battery_ok",    "Battery OK",    DATA_FORMAT, "%u",     DATA_INT,    !battery_low,
                "battery_level", "Battery Level", DATA_INT,    battery_level,
                "mic",           "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    // 0x09 soil sensor -> Water Timer telemetry: the original reading the 0x0a relay forwards (same
    // battery/moisture/temperature block, without soil_rssi). See header 0x09.
    else if (msg_type == 0x09 && msg_length == 0x09) {

        int device_type   = b[11];
        int sensor_number = b[12];
        int battery_low   = (b[14] & 0x10) >> 4;
        int battery_level = (b[14] & 0x0f);
        int moisture      = b[16];
        int temperature_f = (int16_t)((b[19] << 8) | b[18]); // signed: below-zero F is two's complement

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture"
                "msg_name",      "",              DATA_STRING, "Soil telemetry",
                "id",            "",              DATA_FORMAT, "%u",     DATA_INT,    source_id,
                "device_type",   "",              DATA_FORMAT, "%u",     DATA_INT,    device_type,
                "sensor_number", "",              DATA_FORMAT, "%u",     DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",     DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "msg_type",      "",              DATA_FORMAT, "%02X",   DATA_INT,    msg_type,
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
    // 0x04 watering event (see header 0x04): programme b[14:16] (BE), cycle_counter b[16:18] (LE),
    // trigger b[18], water_usage b[19:21] (0.1 L, LE), duration_s b[23:25] (actual elapsed s, LE).
    else if (msg_type == 0x04 && msg_length == 0x0e) {

        int sensor_number = b[11];
        int programme     = (b[14] << 8) | b[15]; // opaque slot id; big-endian on purpose so its %04x hex matches the on-wire byte order (unlike the numeric LE fields below)
        int cycle_counter = b[16] | (b[17] << 8);
        int trigger       = b[18];
        int water_usage   = b[19] | (b[20] << 8); // 0.1 L units; b[21:23] stay 0, so it may be wider
        int duration_s    = b[23] | (b[24] << 8);

        /* clang-format off */
        data_t *data = data_make(
                "model",         "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-WaterTimer"
                "msg_name",      "",              DATA_STRING, "Watering",
                "id",            "",              DATA_FORMAT, "%u",   DATA_INT,    source_id,
                "sensor_number", "",              DATA_FORMAT, "%u",   DATA_INT,    sensor_number,
                "station_id",    "",              DATA_FORMAT, "%u",   DATA_INT,    target_id,
                "msg_counter",   "Msg Counter",   DATA_INT,    counter,
                "msg_type",      "",              DATA_FORMAT, "%02X", DATA_INT,    msg_type,
                "programme",     "",              DATA_FORMAT, "%04x", DATA_INT,    programme,
                "cycle_counter", "",              DATA_INT,    cycle_counter,
                "trigger",       "",              DATA_FORMAT, "%02x", DATA_INT,    trigger,
                "water_usage_l", "Water Usage",   DATA_FORMAT, "%.1f l", DATA_DOUBLE, water_usage * 0.1f,
                "duration_s",    "Duration",      DATA_FORMAT, "%u s", DATA_INT,    duration_s,
                "mic",           "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // 0x85 schedule/duration config, pushed on each app settings change (see header 0x85 for the
    // field semantics; offsets are in the reads below).
    else if (msg_type == 0x85 && msg_length == 0x0f) {

        int default_duration_s = b[12] | (b[13] << 8);
        int mist_run_s         = b[14] | (b[15] << 8);
        int mist_interval_s    = b[16] | (b[17] << 8);
        int sensor_number      = b[18];
        int stop_moisture      = b[19];
        int unknown            = b[22];         // 0x42 observed, meaning unknown
        int flow_rate          = (int8_t)b[24]; // signed: app range -20..+20 %, e.g. -20 -> 0xec

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",              DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "msg_name",        "",              DATA_STRING, "Schedule config",
                "id",              "",              DATA_FORMAT, "%u",   DATA_INT,    source_id,
                "target_id",       "",              DATA_FORMAT, "%u",   DATA_INT,    target_id,
                "sensor_number",   "",              DATA_FORMAT, "%u",   DATA_INT,    sensor_number,
                "msg_counter",     "Msg Counter",   DATA_INT,    counter,
                "msg_type",        "",              DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "default_duration_s", "Default Duration", DATA_FORMAT, "%u s", DATA_INT, default_duration_s,
                "mist_run_s",      "Mist Run",      DATA_FORMAT, "%u s", DATA_INT,    mist_run_s,
                "mist_interval_s", "Mist Interval", DATA_FORMAT, "%u s", DATA_INT,    mist_interval_s,
                "stop_moisture",   "Stop Moisture", DATA_FORMAT, "%u %%", DATA_INT,   stop_moisture,
                "flow_rate",       "Flow Rate",     DATA_FORMAT, "%d %%", DATA_INT,   flow_rate,
                "unknown",         "Unknown",       DATA_FORMAT, "%02x",  DATA_INT,   unknown,
                "mic",             "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // Config-change counter (0x20): b[11] increments on each settings edit. A 3-byte variant also
    // reports the changed setting: b[12] = field id (0x04 = RF communication channel), b[13] = value.
    else if (msg_type == 0x20 && (msg_length == 0x02 || msg_length == 0x03)) {

        int has_channel = (msg_length == 0x03 && b[12] == 0x04); // b[13] = RF channel (1/2/3)

        /* clang-format off */
        data_t *data = data_make(
                "model",          "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "msg_name",       "",            DATA_STRING, "Config change",
                "id",             "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",      "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",    "Msg Counter", DATA_INT,    counter,
                "msg_type",       "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "config_counter", "",            DATA_INT,    b[11],
                "rf_channel",     "RF Channel",  DATA_COND,   has_channel, DATA_INT, b[13],
                "mic",            "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    0x86 schedule table (see header 0x86): a 1-byte more_parts header (b[11]) then one 7-byte
    record per watering plan (fields r[0..6] decoded below), at most 2 records per 33-byte frame.
    NOTE: `plan` is the record's 1-based index WITHIN THIS MESSAGE, not the app's absolute plan
    number (a large table splits across pages with no global index) - identify a plan by start time.
    */
    else if (msg_type == 0x86 && (msg_length == 0x08 || msg_length == 0x0f)) {

        char const *const day_mode[] = {"unknown", "every day", "odd days",
                "even days", "weekly", "unknown", "unknown", "unknown"};
        data_t *plan_data[2]         = {0};
        int np                       = 0;
        int n_plans                  = (msg_length - 1) / 7; // 1-byte header, then 7 bytes per record
        // A 33-byte frame holds at most 2 records (1-byte header + 2*7), so plan_data is sized 2;
        // that cap plus the (12 + p*7 + 6) < sizeof(b) guard keep the reads inside the frame and off
        // the trailing CRC bytes even if msg_length is bogus.
        for (int p = 0; p < n_plans && np < (int)(sizeof(plan_data) / sizeof(plan_data[0])) && (12 + p * 7 + 6) < (int)sizeof(b); p++) {
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
                "msg_name",    "",            DATA_STRING, "Schedule",
                "id",          "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",   "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter", "Msg Counter", DATA_INT,    counter,
                "more_parts",  "",            DATA_INT,    b[11] ? 1 : 0,
                "msg_type",    "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "plans",       "",            DATA_ARRAY,  data_array(np, DATA_DATA, plan_data),
                "mic",         "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // 0x21 base -> Water Timer run command / heartbeat (see header 0x21). variant b[12]: 0x02 = a
    // manual run (mode b[13]: 0 stop / 1 normal / 2 misting; duration_s b[14:16]), else Heartbeat.
    else if (msg_type == 0x21 && msg_length >= 0x03) {

        int variant        = b[12];
        int mode           = b[13];
        int duration_s     = (msg_length >= 0x04) ? b[14] : 0;
        if (msg_length >= 0x05)
            duration_s |= b[15] << 8; // 2-byte LE, like every other duration in this decoder
        int is_run         = (variant == 0x02);
        char const *status = !is_run ? "Heartbeat" : (mode == 0 ? "Run stop" : "Run start");

        char msg[41];
        print_payload_hex(msg, b, msg_length);

        /* clang-format off */
        data_t *data = data_make(
                "model",                "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "msg_name",             "",            DATA_STRING, status,
                "id",                   "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",            "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",          "Msg Counter", DATA_INT,    counter,
                "msg_type",             "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "mode",                 "",            DATA_COND,   is_run, DATA_INT, mode,
                "duration_s",           "Duration",    DATA_COND,   is_run && msg_length >= 0x04, DATA_FORMAT, "%u s", DATA_INT, duration_s,
                "heartbeat_interval_s", "",            DATA_COND,   !is_run && msg_length >= 0x04, DATA_INT, duration_s,
                "msg",                  "",            DATA_STRING, msg,
                "mic",                  "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // 0xa0/0xa1 Water Timer replies to the gateway's control frames (see header 0xa0/0xa1): 0xa0 acks
    // the 0x20 config change (empty); 0xa1 answers a 0x21 run with the run state, else bare "02" Beacon.
    else if (msg_type == 0xa1 || msg_type == 0xa0) {

        /* 0xa1 run-state layout (same markers as the 0x02 report, without its 06 TT 01 prefix):
           00 TT 9f WW WW 00 00 81 RR RR ad DD DD -> trigger b[12], water_usage b[14:16], remaining_s
           b[19:21], duration_s b[22:24]; the 9f/81/ad markers gate has_run. */
        int has_run     = (msg_type == 0xa1 && msg_length >= 0x0d && b[13] == 0x9f && b[18] == 0x81 && b[21] == 0xad);
        int trigger     = has_run ? b[12] : 0;
        int water_usage = has_run ? (b[14] | (b[15] << 8)) : 0; // last COMPLETED run, 0.1 L units
        int remaining_s = has_run ? (b[19] | (b[20] << 8)) : 0;
        int duration_s  = has_run ? (b[22] | (b[23] << 8)) : 0;
        char const *status = (msg_type == 0xa0) ? "Acknowledgement" : (has_run ? "Run response" : "Beacon");
        char msg[41];
        print_payload_hex(msg, b, msg_length);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-WaterTimer"
                "msg_name",        "",            DATA_STRING, status,
                "id",              "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",       "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",     "Msg Counter", DATA_INT,    counter,
                "msg_type",        "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "trigger",         "",            DATA_COND,   has_run, DATA_FORMAT, "%02x", DATA_INT, trigger,
                "duration_s",      "Duration",    DATA_COND,   has_run, DATA_FORMAT, "%u s", DATA_INT, duration_s,
                "remaining_s",     "Remaining",   DATA_COND,   has_run, DATA_FORMAT, "%u s", DATA_INT, remaining_s,
                "water_usage_l",   "Water Usage", DATA_COND,   has_run, DATA_FORMAT, "%.1f l", DATA_DOUBLE, water_usage * 0.1f,
                "acknowledgement", "",            DATA_INT,    acknowledgement,
                "msg",             "",            DATA_STRING, msg,
                "mic",             "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    0x82 gateway reply to the Water Timer's 0x02 status request (0x02|0x80, same counter). Two
    lengths: a short one (length 2) with no clock bytes, and a long one (length >= 5) carrying
    gateway_time = b[13:16] (LE u24), emitted only when present - see header 0x82 for what
    gateway_time is. payload[1] (b[12]) is the gateway's config_counter (the 0x20 value).
    */
    else if (msg_type == 0x82 && msg_length >= 0x02) {

        int config_counter    = b[12]; // payload[1]
        uint32_t gateway_time = (msg_length >= 0x05) ? (uint32_t)(b[13] | (b[14] << 8) | (b[15] << 16)) : 0;
        char msg[41];
        print_payload_hex(msg, b, msg_length);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "msg_name",     "",            DATA_STRING, "Status response",
                "id",           "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",    "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",  "Msg Counter", DATA_INT,    counter,
                "msg_type",     "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "config_counter", "",          DATA_INT,    config_counter,
                "gateway_time", "",            DATA_COND,   msg_length >= 0x05, DATA_INT, (int)gateway_time,
                "msg",          "",            DATA_STRING, msg,
                "mic",          "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // 0x88 gateway reply to the 0x08 moisture request (0x08|0x80). Payload 00 00 MM, moisture % at
    // b[13]. See header 0x88.
    else if (msg_type == 0x88 && msg_length >= 0x03) {

        int moisture = b[13]; // payload[2]
        char msg[41];
        print_payload_hex(msg, b, msg_length);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-Gateway"
                "msg_name",     "",            DATA_STRING, "Moisture response",
                "id",           "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",    "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",  "Msg Counter", DATA_INT,    counter,
                "msg_type",     "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "moisture",     "Moisture",    DATA_FORMAT, "%u %%", DATA_INT, moisture,
                "msg",          "",            DATA_STRING, msg,
                "mic",          "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Water Timer -> gateway polls; the gateway replies with type|0x80 (echoing the counter). Only the
    0x02 poll carries data: its 15-byte payload holds the live run state behind the 0x9f/0x81/0xad
    markers (see header 0x02) - trigger b[14], remaining_s b[21:23] (starts at duration_s + 1, counts
    down 1/s), duration_s b[24:26]; all 0 when idle. 0x05/0x06/0x08 are bare fetches whose data comes
    back in the 0x85/0x86/0x88 replies (0x06 payload byte[2] selects a schedule page).
    */
    else if (msg_type == 0x02 || msg_type == 0x05 || msg_type == 0x06 || msg_type == 0x08) {

        char const *status = (msg_type == 0x02)   ? "Status report"
                             : (msg_type == 0x05) ? "Config request"
                             : (msg_type == 0x08) ? "Moisture request"
                                                  : "Schedule request";
        char msg[41];
        print_payload_hex(msg, b, msg_length);

        // Only the 0x02 poll carries the run state, and only when both markers are in place.
        int has_run     = (msg_type == 0x02 && msg_length >= 0x0f && b[20] == 0x81 && b[23] == 0xad);
        int trigger     = has_run ? b[14] : 0;
        int water_usage = has_run ? (b[16] | (b[17] << 8)) : 0; // last COMPLETED run, 0.1 L units
        int remaining_s = has_run ? (b[21] | (b[22] << 8)) : 0;
        int duration_s  = has_run ? (b[24] | (b[25] << 8)) : 0;

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-WaterTimer"
                "msg_name",     "",            DATA_STRING, status,
                "id",           "",            DATA_FORMAT, "%u", DATA_INT, source_id,
                "target_id",    "",            DATA_FORMAT, "%u", DATA_INT, target_id,
                "msg_counter",  "Msg Counter", DATA_INT,    counter,
                "msg_type",     "",            DATA_FORMAT, "%02X", DATA_INT, msg_type,
                "msg_length",   "",            DATA_FORMAT, "%02X", DATA_INT, msg_length,
                "trigger",      "",            DATA_COND,   has_run, DATA_FORMAT, "%02x", DATA_INT, trigger,
                "duration_s",   "Duration",    DATA_COND,   has_run, DATA_FORMAT, "%u s", DATA_INT, duration_s,
                "remaining_s",  "Remaining",   DATA_COND,   has_run, DATA_FORMAT, "%u s", DATA_INT, remaining_s,
                "water_usage_l", "Water Usage", DATA_COND,  has_run, DATA_FORMAT, "%.1f l", DATA_DOUBLE, water_usage * 0.1f,
                "msg",          "",            DATA_STRING, msg,
                "mic",          "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    /*
    Not-yet-decoded control frames fall through here and are emitted with their raw `msg` rather than
    guessed. Seen during a Water Timer re-pair, still opaque:
      0x81 len 11  base -> valve, e.g. "000506e00110c7e8190137" - role unknown.
    */
    else {

        // Not yet decoded message type - emit its raw sub-message (msg_length bytes) for analysis
        char msg[41];
        print_payload_hex(msg, b, msg_length);

        /* clang-format off */
        data_t *data = data_make(
                "model",           "",            DATA_STRING, bresser_garden_model(source_id), // "Bresser-SoilMoisture" "Bresser-WaterTimer" "Bresser-Gateway" "Bresser-Garden"
                "msg_name",        "",            DATA_STRING, "Unknown msg",
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
        "msg_name",
        "firmware",
        "moisture",
        "soil_rssi",
        "programme",
        "cycle_counter",
        "trigger",
        "mode",
        "duration_s",
        "remaining_s",
        "default_duration_s",
        "mist_run_s",
        "mist_interval_s",
        "stop_moisture",
        "flow_rate",
        "config_counter",
        "rf_channel",
        "gateway_time",
        "plans",
        "more_parts",
        "plan",
        "enabled",
        "irrigation",
        "start_hour",
        "start_minute",
        "day_mode",
        "weekday_mask",
        "water_limit_l",
        "water_usage_l",
        "unknown",
        "heartbeat_interval_s",
        "battery_ok",
        "battery_level",
        "acknowledgement",
        "msg_type",
        "msg_length",
        "msg",
        "mic",
        NULL,
};

r_device const bresser_garden = {
        .name        = "Bresser SmartHome Garden soil moisture and water timer valve (Baldr Homgar, RainPoint)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 10000, // long part of the message could be zeros
        .decode_fn   = &bresser_garden_decode,
        .fields      = output_fields,
};
