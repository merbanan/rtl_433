"""ThermoPro TP211B Thermometer.

RF:
- 915 MHz FSK temperature sensor.

Flex decoder:

    rtl_433 -f 915M -X "n=tp211b,m=FSK_PCM,s=105,l=105,r=1500,preamble=552dd4"

Data layout after preamble:

    Byte Position   0  1  2  3  4  5  6  7
    Sample          01 1e d6 03 6c aa 14 ff
    Sample          01 1e d6 02 fa aa c4 1e
                    II II II fT TT aa CC CC

- III: {24} Sensor ID
- f:   {4}  Flags or unused, always 0
- TTT: {12} Temperature, raw value, deg C = (raw - 500) / 10
- aa:  {8}  Fixed value 0xAA
- CC:  {16} Checksum, XOR table based (see thermopro_tp211b.h).
"""

from proto_compiler.dsl import BitbufferPipeline, Bits, DecodeFail, JsonRecord, Literal, Modulation, ModulationConfig, Decoder


class thermopro_tp211b(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="ThermoPro TP211B Thermometer",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=105,
            long_width=105,
            reset_limit=1500,
        )

    def prepare(self, buf: BitbufferPipeline) -> BitbufferPipeline:
        return buf.search_preamble(0x552DD4, bit_length=24).skip_bits(24)

    id: Bits[24]
    flags: Bits[4]
    temp_raw: Bits[12]
    fixed_aa: Literal[0xAA, 8]
    checksum: Bits[16]

    def validate_checksum(self, id, flags, temp_raw, checksum, fail_value=DecodeFail.MIC) -> bool: ...

    def temperature_c(self, temp_raw) -> float:
        return (temp_raw - 500) * 0.1

    def to_json(self) -> list[JsonRecord]:
        return [
            JsonRecord("model", "", "ThermoPro-TP211B", "DATA_STRING"),
            JsonRecord("id", "", self.id, "DATA_INT"),
            JsonRecord("temperature_c", "Temperature", self.temperature_c, "DATA_DOUBLE", fmt="%.1f C"),
            JsonRecord("mic", "Integrity", "CHECKSUM", "DATA_STRING"),
        ]


decoders = (thermopro_tp211b(),)
