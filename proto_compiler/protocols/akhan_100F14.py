"""Akhan 100F14 remote keyless entry.

RKE using HS1527-style framing: preamble, 20-bit id, 4-bit command.
Row is 25 bits; the last bit is consumed as padding after the 24-bit message.

Reference: src/devices/akhan_100F14.c (hand-written; this module replaces it).
"""

from proto_compiler.dsl import Bits, F, JsonRecord, Modulation, ModulationConfig, Protocol


class akhan_100F14(Protocol):
    class modulation_config(ModulationConfig):
        device_name = "Akhan 100F14 remote keyless entry"
        output_model = "Akhan-100F14"
        modulation = Modulation.OOK_PULSE_PWM
        short_width = 316
        long_width = 1020
        reset_limit = 1800
        sync_width = 0
        tolerance = 80
        disabled = 1

    def prepare(self):
        return self.bitbuffer.find_repeated_row(1, 25)

    b0: Bits[8]
    b1: Bits[8]
    b2: Bits[8]
    _trail: Bits[1]

    def notb0(self) -> int:
        return (~F.b0) & 0xFF

    def notb1(self) -> int:
        return (~F.b1) & 0xFF

    def notb2(self) -> int:
        return (~F.b2) & 0xFF

    def id(self) -> int:
        return (F.notb0 << 12) | (F.notb1 << 4) | (F.notb2 >> 4)

    def cmd(self) -> int:
        return F.notb2 & 0x0F

    def validate(self):
        # Methods run before properties; cannot use ``cmd`` here (not emitted yet).
        # ``cmd`` == ``((~b2) & 0xFF) & 0x0F`` (same as ``notb2 & 0x0F``).
        return (
            ((((~F.b2) & 0xFF) & 0x0F) == 1)
            | ((((~F.b2) & 0xFF) & 0x0F) == 2)
            | ((((~F.b2) & 0xFF) & 0x0F) == 4)
            | ((((~F.b2) & 0xFF) & 0x0F) == 8)
        )

    def data_str(self, cmd) -> str: ...

    def to_json(self) -> list[JsonRecord]:
        return [
            JsonRecord("model", "", "Akhan-100F14", "DATA_STRING"),
            JsonRecord("id", "ID (20bit)", self.id, "DATA_INT", fmt="0x%x"),
            JsonRecord("data", "Data (4bit)", self.data_str, "DATA_STRING"),
        ]
