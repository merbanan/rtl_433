"""LaCrosse TX31U – Repeat, sub-protocols, CRC-8."""

from proto_compiler.dsl import (
    Bits,
    F,
    Literal,
    Modulation,
    Protocol,
    ProtocolConfig,
    Repeat,
)


class sensor_reading(Protocol):
    sensor_type: Bits[4]
    reading: Bits[12]


class lacrosse_tx31u(Protocol):
    class config(ProtocolConfig):
        device_name = "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 116
        long_width = 116
        reset_limit = 20000
        preamble = 0xAAAA2DD4

    _model: Literal[0xA, 4]
    sensor_id: Bits[6]
    training: Bits[1]
    no_ext: Bits[1]
    battery_low: Bits[1]
    measurements: Bits[3]
    readings: Repeat[F.measurements, sensor_reading]
    crc: Bits[8]

    def validate(self, b, measurements, crc): ...

    def temperature_c(
        self, readings_sensor_type, readings_reading, measurements
    ) -> float: ...

    def humidity(self, readings_sensor_type, readings_reading, measurements) -> int: ...
