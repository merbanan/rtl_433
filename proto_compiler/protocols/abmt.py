"""Amazon Basics Meat Thermometer

Manchester encoded PCM signal.

[00] {48} e4 00 a3 01 40 ff

II 00 UU TT T0 FF

I - power on random id
0 - zeros
U - Unknown
T - BCD coded temperature
F - ones
"""

from proto_compiler.dsl import Bits, JsonRecord, Modulation, ModulationConfig, Protocol


class abmt(Protocol):
    class modulation_config(ModulationConfig):
        device_name = "Amazon Basics Meat Thermometer"
        output_model = "Basics-Meat"
        modulation = Modulation.OOK_PULSE_PCM
        short_width = 550
        long_width = 550
        gap_limit = 2000
        reset_limit = 5000

    def prepare(self):
        return (
            self.bitbuffer
            .find_repeated_row(4, 90, max_bits=120)
            .search_preamble(0x55AAAA, bit_length=24)
            .skip_bits(-72)
            .manchester_decode(48)
            .invert()
        )

    id: Bits[8]
    _zero0: Bits[8]
    _unknown: Bits[8]
    temp_bcd_byte: Bits[8]
    temp_last_byte: Bits[8]
    _trailer: Bits[8]

    def temperature_c(self) -> float:
        return (10 * (self.temp_bcd_byte >> 4) + (self.temp_bcd_byte & 0xF)) * 10 + (
            self.temp_last_byte >> 4
        )

    def to_json(self) -> list[JsonRecord]:
        # fmt: off
        return [
            JsonRecord("model", "", "Basics-Meat", "DATA_STRING"),
            JsonRecord("id", "Id", self.id, "DATA_INT"),
            JsonRecord(
                "temperature_C",
                "Temperature",
                self.temperature_c,
                "DATA_DOUBLE",
                fmt="%.1f C"
            ),
        ]
        # fmt: on
