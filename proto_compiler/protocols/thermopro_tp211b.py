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

from proto_compiler.dsl import Bits, Literal, Modulation, Protocol, ProtocolConfig


class thermopro_tp211b(Protocol):
    class config(ProtocolConfig):
        device_name = "ThermoPro TP211B Thermometer"
        output_model = "ThermoPro-TP211B"
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
