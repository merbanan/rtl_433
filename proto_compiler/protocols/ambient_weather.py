"""Ambient Weather F007TH thermo-hygrometer (and related senders).

Generated decoder; LFSR MIC and sanity checks are inlined via ``validate_*`` hooks
and ``LfsrDigest8``.

Differs from the old hand-written decoder: only preamble ``0x145`` (12 bits) is
searched, first match per row from bit 0 only (no second inverted preamble, no
sliding retry after MIC failure).

Reference: former ``src/devices/ambient_weather.c``.
"""

from proto_compiler.dsl import (
    BitbufferPipeline,
    Bits,
    DecodeFail,
    Decoder,
    JsonRecord,
    LfsrDigest8,
    Modulation,
    ModulationConfig,
)


class ambient_weather(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Ambient Weather F007TH, TFA 30.3208.02, SwitchDocLabs F016TH temperature sensor",
            modulation=Modulation.OOK_PULSE_MANCHESTER_ZEROBIT,
            short_width=500,
            long_width=0,
            reset_limit=2400,
        )

    def prepare(self, buf: BitbufferPipeline) -> BitbufferPipeline:
        return buf.search_preamble(0x145, bit_length=12, scan_all_rows=True).skip_bits(8)

    b0: Bits[8]
    b1: Bits[8]
    b2: Bits[8]
    b3: Bits[8]
    b4: Bits[8]
    b5: Bits[8]

    def validate_mic(
        self, b0, b1, b2, b3, b4, b5, fail_value=DecodeFail.MIC
    ) -> bool:
        return (LfsrDigest8(b0, b1, b2, b3, b4, gen=0x98, key=0x3E) ^ 0x64) == b5

    def validate_sanity_humidity(self, b4, fail_value=DecodeFail.SANITY) -> bool:
        return b4 <= 100

    def validate_sanity_temperature(self, b2, b3, fail_value=DecodeFail.SANITY) -> bool:
        return (((b2 & 0x0F) << 8) | b3) < 3840

    def sensor_id(self, b1) -> int:
        return b1

    def battery_ok(self, b2) -> int:
        return (b2 & 0x80) == 0

    def channel(self, b2) -> int:
        return ((b2 & 0x70) >> 4) + 1

    def temperature_F(self, b2, b3) -> float:
        return ((((b2 & 0x0F) << 8) | b3) - 400) * 0.1

    def humidity(self, b4) -> int:
        return b4

    def to_json(self) -> list[JsonRecord]:
        return [
            JsonRecord("model", "", "Ambientweather-F007TH", "DATA_STRING"),
            JsonRecord("id", "House Code", self.sensor_id, "DATA_INT"),
            JsonRecord("channel", "Channel", self.channel, "DATA_INT"),
            JsonRecord("battery_ok", "Battery", self.battery_ok, "DATA_INT"),
            JsonRecord(
                "temperature_F",
                "Temperature",
                self.temperature_F,
                "DATA_DOUBLE",
                fmt="%.1f F",
            ),
            JsonRecord("humidity", "Humidity", self.humidity, "DATA_INT", fmt="%u %%"),
            JsonRecord("mic", "Integrity", "CRC", "DATA_STRING"),
        ]


decoders = (ambient_weather(),)
