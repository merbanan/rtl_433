"""FSK 8 byte Manchester encoded TPMS with simple checksum.

Seen on Ford Fiesta, Focus, Kuga, Escape, Transit...

Seen on 315.00 MHz (United States) and 433.92 MHz.
Likely VDO-Sensors, Type "S180084730Z", built by "Continental Automotive GmbH".

Packet nibbles:

    II II II II PP TT FF CC

- I = ID
- P = Pressure, as PSI * 4
- T = Temperature, as C + 56
- F = Flags
- C = Checksum, SUM bytes 0 to 6 = byte 7
"""

from proto_compiler.dsl import Bits, JsonRecord, Modulation, Protocol, ProtocolConfig


class tpms_ford(Protocol):
    class config(ProtocolConfig):
        device_name = "Ford TPMS"
        output_model = "Ford"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 52
        long_width = 52
        reset_limit = 150
        invert = True
        preamble = 0xAAA9
        preamble_bit_length = 16
        manchester = True
        manchester_start_offset_bits = 16
        manchester_decode_max_bits = 160
        scan_rows = "all"

    sensor_id: Bits[32]
    pressure_raw: Bits[8]
    temp_byte: Bits[8]
    flags: Bits[8]
    checksum: Bits[8]

    def validate(self):
        return (
            (
                (self.sensor_id >> 24)
                + ((self.sensor_id >> 16) & 0xFF)
                + ((self.sensor_id >> 8) & 0xFF)
                + (self.sensor_id & 0xFF)
                + self.pressure_raw
                + self.temp_byte
                + self.flags
            )
            & 0xFF
        ) == self.checksum

    def pressure_psi(self) -> float:
        return (((self.flags & 0x20) << 3) | self.pressure_raw) * 0.25

    def temperature_c(self) -> float:
        return (self.temp_byte & 0x7F) - 56

    def moving(self) -> int:
        return (self.flags & 0x44) == 0x44

    def learn(self) -> int:
        return (self.flags & 0x4C) == 0x08

    def code(self) -> int:
        return (self.pressure_raw << 16) | (self.temp_byte << 8) | self.flags

    def unknown(self) -> int:
        return self.flags & 0x90

    def unknown_3(self) -> int:
        return self.flags & 0x03

    def to_json(self) -> list[JsonRecord]:
        # fmt: off
        return [
            JsonRecord("model",        "",          "Ford",           "DATA_STRING"),
            JsonRecord("type",         "",          "TPMS",           "DATA_STRING"),
            JsonRecord("id",           "",          self.sensor_id,   "DATA_STRING", fmt="%08x"),
            JsonRecord("pressure_PSI", "Pressure",  self.pressure_psi, "DATA_DOUBLE"),
            JsonRecord("moving",       "Moving",    self.moving,      "DATA_INT"),
            JsonRecord("learn",        "Learn",     self.learn,       "DATA_INT"),
            JsonRecord("code",         "",          self.code,        "DATA_STRING", fmt="%06x"),
            JsonRecord("unknown",      "",          self.unknown,     "DATA_STRING", fmt="%02x"),
            JsonRecord("unknown_3",    "",          self.unknown_3,   "DATA_STRING", fmt="%01x"),
            JsonRecord("mic",          "Integrity", "CHECKSUM",       "DATA_STRING"),
        ]  # fmt: on
