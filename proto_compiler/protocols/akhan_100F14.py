"""Akhan 100F14 remote keyless entry.

RKE using HS1527-style framing: preamble, 20-bit id, 4-bit command.
Row is 25 bits; the last bit is consumed as padding after the 24-bit message.

Reference: src/devices/akhan_100F14.c (hand-written; this module replaces it).
"""

from proto_compiler.dsl import (
    BitbufferPipeline,
    Bits,
    DecodeFail,
    Decoder,
    JsonRecord,
    Modulation,
    ModulationConfig,
)


class akhan_100F14(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Akhan 100F14 remote keyless entry",
            modulation=Modulation.OOK_PULSE_PWM,
            short_width=316,
            long_width=1020,
            reset_limit=1800,
            sync_width=0,
            tolerance=80,
            disabled=1,
        )

    def prepare(self, buf: BitbufferPipeline) -> BitbufferPipeline:
        return buf.find_repeated_row(1, 25)

    b0: Bits[8]
    b1: Bits[8]
    b2: Bits[8]
    _trail: Bits[1]

    def notb0(self, b0) -> int:
        return (~b0) & 0xFF

    def notb1(self, b1) -> int:
        return (~b1) & 0xFF

    def notb2(self, b2) -> int:
        return (~b2) & 0xFF

    def id(self, notb0, notb1, notb2) -> int:
        return (notb0 << 12) | (notb1 << 4) | (notb2 >> 4)

    def cmd(self, notb2) -> int:
        return notb2 & 0x0F

    def validate_cmd(self, cmd, fail_value=DecodeFail.SANITY) -> bool:
        return (cmd == 1) | (cmd == 2) | (cmd == 4) | (cmd == 8)

    def data_str(self, cmd) -> str: ...

    def to_json(self) -> list[JsonRecord]:
        return [
            JsonRecord("model", "", "Akhan-100F14", "DATA_STRING"),
            JsonRecord("id", "ID (20bit)", self.id, "DATA_INT", fmt="0x%x"),
            JsonRecord("data", "Data (4bit)", self.data_str, "DATA_STRING"),
        ]


decoders = (akhan_100F14(),)
