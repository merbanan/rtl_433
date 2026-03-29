"""ThermoPro TP211B – flat fields, literal assertion, checksum, property."""

from proto_compiler.dsl import Bits, Literal, Modulation, Protocol, ProtocolConfig


class thermopro_tp211b(Protocol):
    class config(ProtocolConfig):
        device_name = "ThermoPro TP211B Thermometer"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 105
        long_width = 105
        reset_limit = 1500
        preamble = 0x552DD4

    id: Bits[24]
    flags: Bits[4]
    temp_raw: Bits[12]
    fixed_aa: Literal[0xAA, 8]
    checksum: Bits[16]

    def validate(self, b, checksum): ...

    def temperature_c(self) -> float:
        return (self.temp_raw - 500) * 0.1
