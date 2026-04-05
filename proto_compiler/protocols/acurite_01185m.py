"""Decoder for Acurite Grill/Meat Thermometer 01185M.

Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
Based on work by Joe "exeljb"

Modulation:

- 56 bit PWM data
- short is 840 us pulse, 2028 us gap
- long is 2070 us pulse, 800 us gap,
- sync is 6600 us pulse, 4080 gap,
- there is no packet gap and 8 repeats
- data is inverted (short=0, long=1) and byte-reflected

S.a. #1824

Temperature is 16 bit, degrees F, scaled x10 +900.
The first reading is the "Meat" channel and the second is for the "Ambient" or
grill temperature.

- A value of 0x1b58 (7000 / 610F) indicates the sensor is unplugged (E1).
- A value of 0x00c8 (200 / -70F) indicates a sensor problem (E2).

The battery status is the MSB of the second byte, 0 for good battery, 1 for low.

Channel appears random (often 3, 6, 12, 15).

Data layout (56 bits):

    II BC MM MM TT TT XX

- I: 8 bit ID
- B: 1 bit battery-low (MSB of second byte); middle bits skipped below
- C: 4 bit channel (low nibble of second byte)
- M: 16 bit temperature 1 (F x10 +900)
- T: 16 bit temperature 2
- X: 8 bit checksum, add-with-carry over first 6 bytes
"""

from proto_compiler.dsl import Bits, JsonRecord, Modulation, ModulationConfig, Protocol


class acurite_01185m(Protocol):
    class modulation_config(ModulationConfig):
        device_name = "Acurite Grill/Meat Thermometer 01185M"
        modulation = Modulation.OOK_PULSE_PWM
        short_width = 840
        long_width = 2070
        sync_width = 6600
        gap_limit = 3000
        reset_limit = 6000

    def prepare(self):
        return (
            self.bitbuffer
            .invert()
            .first_valid_row(56, reflect=True, checksum_over_bytes=6)
        )

    id: Bits[8]
    battery_low: Bits[1]
    _mid: Bits[3]
    channel: Bits[4]
    temp1_raw: Bits[16]
    temp2_raw: Bits[16]
    _checksum: Bits[8]

    def temp1_ok(self) -> int:
        return (self.temp1_raw > 200) & (self.temp1_raw < 7000)

    def temp2_ok(self) -> int:
        return (self.temp2_raw > 200) & (self.temp2_raw < 7000)

    def temp1_f(self) -> float:
        return (self.temp1_raw - 900) * 0.1

    def temp2_f(self) -> float:
        return (self.temp2_raw - 900) * 0.1

    def battery_ok(self) -> int:
        return self.battery_low == 0

    def to_json(self) -> list[JsonRecord]:
        # fmt: off
        return [
            JsonRecord("model", "", "Acurite-01185M", "DATA_STRING"),
            JsonRecord("id", "", self.id, "DATA_INT"),
            JsonRecord("channel", "", self.channel, "DATA_INT"),
            JsonRecord("battery_ok", "Battery", self.battery_ok, "DATA_INT"),
            JsonRecord(
                "temperature_1_F",
                "Meat",
                self.temp1_f,
                "DATA_DOUBLE",
                fmt="%.1f F",
                when=self.temp1_ok,
            ),
            JsonRecord(
                "temperature_2_F",
                "Ambient",
                self.temp2_f,
                "DATA_DOUBLE",
                fmt="%.1f F",
                when=self.temp2_ok,
            ),
            JsonRecord("mic", "Integrity", "CHECKSUM", "DATA_STRING"),
        ]
        # fmt: on
