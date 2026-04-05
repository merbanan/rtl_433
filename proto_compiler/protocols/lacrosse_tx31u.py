"""LaCrosse TX31U-IT protocol.

Decoder for LaCrosse transmitter provided with the WS-1910TWC-IT product.
Branded with "The Weather Channel" logo.

FCC ID: OMO-TX22U
FSK_PCM @915 MHz, 116usec/bit

Protocol
--------

This transmitter uses a variable length protocol that includes 1-5 measurements
of 2 bytes each.  The first nibble of each measurement identifies the sensor.

    Sensor      Code    Encoding
    TEMP          0       BCD tenths of a degree C plus 400 offset.
                              EX: 0x0653 is 25.3 degrees C
    HUMID         1       BCD % relative humidity.
                              EX: 0x1068 is 68%
    UNKNOWN       2       This is probably reserved for a rain gauge (TX32U-IT) - NOT TESTED
    WIND_AVG_DIR  3       Wind direction and decimal time averaged wind speed in m/sec.
                              First nibble is direction in units of 22.5 degrees.
    WIND_MAX      4       Decimal maximum wind speed in m/sec during last reporting interval.
                              First nibble is 0x1 if wind sensor input is lost.

Data layout:

       a    a    a    a    2    d    d    4    a    2    e    5    0    6    5    3    c    0
    Bits :
    1010 1010 1010 1010 0010 1101 1101 0100 1010 0010 1110 0101 0000 0110 0101 0011 1100 0000
    ~~~~~~~~~~~~~~~~~~~ 2 bytes preamble (0xaaaa)
                        ~~~~~~~~~~~~~~~~~~~ bytes 3 and 4 sync word of 0x2dd4
    sensor model (always 0xa)               ~~~~
    Random device id (6 bits)                    ~~~~ ~~
    Initial training mode (all sensors report)          ~
    no external sensor detected                          ~
    low battery indication                                 ~
    count of sensors reporting (1 to 5)                     ~~~
    sensor code                                                 ~~~~
    sensor reading (meaning varies, see above)                       ~~~~ ~~~~ ~~~~
    ---
    --- repeat sensor code:reading as specified in count value above
    ---
    crc8 (poly 0x31 init 0x00) of bytes 5 thru (N-1)                                ~~~~ ~~~~

The WS-1910TWC-IT does not have a rain gauge or wind direction vane.  The readings output here
are inferred from the output data, and correlating it with other similar Lacrosse devices.
These readings have not been tested.
"""

from proto_compiler.dsl import (
    Bits,
    F,
    Literal,
    Modulation,
    ModulationConfig,
    Protocol,
    Repeat,
)


class sensor_reading(Protocol):
    sensor_type: Bits[4]
    reading: Bits[12]


class lacrosse_tx31u(Protocol):
    class modulation_config(ModulationConfig):
        device_name = "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT"
        output_model = "LaCrosse-TX31UIT"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 116
        long_width = 116
        reset_limit = 20000

    def prepare(self):
        return self.bitbuffer.search_preamble(0xAAAA2DD4).skip_bits(32)

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
