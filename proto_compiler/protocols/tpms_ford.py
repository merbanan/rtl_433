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

from proto_compiler.dsl import BitbufferPipeline, Bits, DecodeFail, Decoder, JsonRecord, Modulation, ModulationConfig


class tpms_ford(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Ford TPMS",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=52,
            long_width=52,
            reset_limit=150,
        )

    def prepare(self, buf: BitbufferPipeline) -> BitbufferPipeline:
        return (
            buf
            .invert()
            .search_preamble(0xAAA9, bit_length=16, scan_all_rows=True)
            .skip_bits(16)
            .manchester_decode(160)
        )

    sensor_id: Bits[32]
    pressure_raw: Bits[8]
    temp_byte: Bits[8]
    flags: Bits[8]
    checksum: Bits[8]

    def validate_checksum(
        self,
        sensor_id,
        pressure_raw,
        temp_byte,
        flags,
        checksum,
        fail_value=DecodeFail.MIC,
    ) -> bool:
        return (
            (
                (sensor_id >> 24)
                + ((sensor_id >> 16) & 0xFF)
                + ((sensor_id >> 8) & 0xFF)
                + (sensor_id & 0xFF)
                + pressure_raw
                + temp_byte
                + flags
            )
            & 0xFF
        ) == checksum

    def pressure_psi(self, flags, pressure_raw) -> float:
        return (((flags & 0x20) << 3) | pressure_raw) * 0.25

    def moving(self, flags) -> int:
        return (flags & 0x44) == 0x44

    def learn(self, flags) -> int:
        return (flags & 0x4C) == 0x08

    def code(self, pressure_raw, temp_byte, flags) -> int:
        return (pressure_raw << 16) | (temp_byte << 8) | flags

    def unknown(self, flags) -> int:
        return flags & 0x90

    def unknown_3(self, flags) -> int:
        return flags & 0x03

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


decoders = (tpms_ford(),)
