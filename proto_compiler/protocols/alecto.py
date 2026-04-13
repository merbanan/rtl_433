"""AlectoV1 Weather Sensor decoder (proto_compiler Rows + Variants → ``alecto.c``).

Documentation also at http://www.tfd.hu/tfdhu/files/wsprotocol/auriol_protocol_v20.pdf

Also Unitec W186-F (bought from Migros).

PPM with pulse width 500 us, long gap 4000 us, short gap 2000 us, sync gap 9000 us.

Some sensors transmit 8 long pulses (1-bits) as first row.
Some sensors transmit 3 lone pulses (sync bits) between packets.

Message Format: (9 nibbles, 36 bits):
Please note that bytes need to be reversed before processing!

Format for Temperature Humidity:

    IIIICCII BMMP TTTT TTTT TTTT HHHHHHHH CCCC
    RC       Type Temperature___ Humidity Checksum

- I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
- B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
- M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
- P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
- T: 12 bit Temperature (two's complement)
- H: 8 bit Humidity BCD format
- C: 4 bit Checksum

Format for Rain:

    IIIIIIII BMMP 1100 RRRR RRRR RRRR RRRR CCCC
    RC       Type      Rain                Checksum

- I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
- B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
- M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
- P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
- R: 16 bit Rain (bitvalue * 0.25 mm)
- C: 4 bit Checksum

Format for Windspeed:

    IIIIIIII BMMP 1000 0000 0000 WWWWWWWW CCCC
    RC       Type                Windspd  Checksum

- I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
- B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
- M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
- P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
- W: 8 bit Windspeed  (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
- C: 4 bit Checksum

Format for Winddirection & Windgust:

    IIIIIIII BMMP 111D DDDD DDDD GGGGGGGG CCCC
    RC       Type      Winddir   Windgust Checksum

- I: 8 bit Random Device ID, includes 2 bit channel (X, 1, 2, 3)
- B: 1 bit Battery status (0 normal, 1 voltage is below ~2.6 V)
- M: 2 bit Message type, Temp/Humidity if not '11' else wind/rain sensor
- P: 1 bit a 0 indicates regular transmission, 1 indicates requested by pushbutton
- D: 9 bit Wind direction
- G: 8 bit Windgust (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
- C: 4 bit Checksum

Derived from the former ``src/devices/alecto.c``; PPM timing unchanged.
"""

from proto_compiler.dsl import (
    BitbufferPipeline,
    Bits,
    DecodeFail,
    Decoder,
    JsonRecord,
    Modulation,
    ModulationConfig,
    Repeatable,
    Rev8,
    Rows,
    Variant,
)


class AlectoRow(Repeatable):
    """36 bits per row as five chunks (same layout for each populated row)."""

    b0: Bits[8]
    b1: Bits[8]
    b2: Bits[8]
    b3: Bits[8]
    b4: Bits[4]


class alectov1(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="AlectoV1 Weather Sensor (Alecto WS3500 WS4500 Ventus W155/W044 Oregon)",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=2000,
            long_width=4000,
            gap_limit=7000,
            reset_limit=10000,
        )

    def prepare(self, buf: BitbufferPipeline) -> BitbufferPipeline:
        return buf

    def validate_packet(self, buf: BitbufferPipeline, fail_value=DecodeFail.SANITY) -> bool:
        """Row parity + checksum on rows 1 and 5. Implemented in ``alectov1.h``."""
        ...

    cells: Rows[(1, 2, 3, 4, 5, 6), AlectoRow, ((1, 36),)]

    class Wind0(Variant):
        def when(self, cells) -> bool:
            return (
                (((cells[1].b1 & 0x60) >> 5) == 0x3)
                & ((cells[1].b1 & 0x0F) != 0x0C)
                & ((cells[1].b1 & 0xE) == 0x8)
                & (cells[1].b2 == 0)
            )

        def sensor_id(self, cells) -> int:
            return Rev8(cells[1].b0)

        def channel(self, cells) -> int:
            return (cells[1].b0 & 0xC) >> 2

        def battery_ok(self, cells) -> int:
            return ((cells[1].b1 & 0x80) >> 7) == 0

        def wind_avg_m_s(self, cells) -> float:
            return Rev8(cells[1].b3) * 0.2

        def wind_max_m_s(self, cells) -> float:
            return Rev8(cells[5].b3) * 0.2

        def wind_dir_deg(self, cells) -> int:
            return (Rev8(cells[5].b2) << 1) | (cells[5].b1 & 1)

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "AlectoV1-Wind", "DATA_STRING"),
                JsonRecord("id", "House Code", self.sensor_id, "DATA_INT"),
                JsonRecord("channel", "Channel", self.channel, "DATA_INT"),
                JsonRecord("battery_ok", "Battery", self.battery_ok, "DATA_INT"),
                JsonRecord(
                    "wind_avg_m_s",
                    "Wind speed",
                    self.wind_avg_m_s,
                    "DATA_DOUBLE",
                    fmt="%.2f m/s",
                ),
                JsonRecord(
                    "wind_max_m_s",
                    "Wind gust",
                    self.wind_max_m_s,
                    "DATA_DOUBLE",
                    fmt="%.2f m/s",
                ),
                JsonRecord(
                    "wind_dir_deg", "Wind Direction", self.wind_dir_deg, "DATA_INT"
                ),
                JsonRecord("mic", "Integrity", "CHECKSUM", "DATA_STRING"),
            ]

    class Wind4(Variant):
        def when(self, cells) -> bool:
            return (
                (((cells[1].b1 & 0x60) >> 5) == 0x3)
                & ((cells[1].b1 & 0x0F) != 0x0C)
                & ((cells[1].b1 & 0xE) == 0xE)
            )

        def sensor_id(self, cells) -> int:
            return Rev8(cells[1].b0)

        def channel(self, cells) -> int:
            return (cells[1].b0 & 0xC) >> 2

        def battery_ok(self, cells) -> int:
            return ((cells[1].b1 & 0x80) >> 7) == 0

        def wind_avg_m_s(self, cells) -> float:
            return Rev8(cells[5].b3) * 0.2

        def wind_max_m_s(self) -> float: ...  # TODO: bitbuffer arg

        def wind_dir_deg(self) -> int: ...  # TODO: bitbuffer arg

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "AlectoV1-Wind", "DATA_STRING"),
                JsonRecord("id", "House Code", self.sensor_id, "DATA_INT"),
                JsonRecord("channel", "Channel", self.channel, "DATA_INT"),
                JsonRecord("battery_ok", "Battery", self.battery_ok, "DATA_INT"),
                JsonRecord(
                    "wind_avg_m_s",
                    "Wind speed",
                    self.wind_avg_m_s,
                    "DATA_DOUBLE",
                    fmt="%.2f m/s",
                ),
                JsonRecord(
                    "wind_max_m_s",
                    "Wind gust",
                    self.wind_max_m_s,
                    "DATA_DOUBLE",
                    fmt="%.2f m/s",
                ),
                JsonRecord(
                    "wind_dir_deg", "Wind Direction", self.wind_dir_deg, "DATA_INT"
                ),
                JsonRecord("mic", "Integrity", "CHECKSUM", "DATA_STRING"),
            ]

    class Rain(Variant):
        def when(self, cells) -> bool:
            return (((cells[1].b1 & 0x60) >> 5) == 0x3) & ((cells[1].b1 & 0x0F) == 0x0C)

        def sensor_id(self, cells) -> int:
            return Rev8(cells[1].b0)

        def channel(self, cells) -> int:
            return (cells[1].b0 & 0xC) >> 2

        def battery_ok(self, cells) -> int:
            return ((cells[1].b1 & 0x80) >> 7) == 0

        def rain_mm(self, cells) -> float:
            return ((Rev8(cells[1].b3) << 8) | Rev8(cells[1].b2)) * 0.25

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "AlectoV1-Rain", "DATA_STRING"),
                JsonRecord("id", "House Code", self.sensor_id, "DATA_INT"),
                JsonRecord("channel", "Channel", self.channel, "DATA_INT"),
                JsonRecord("battery_ok", "Battery", self.battery_ok, "DATA_INT"),
                JsonRecord(
                    "rain_mm", "Total Rain", self.rain_mm, "DATA_DOUBLE", fmt="%.2f mm"
                ),
                JsonRecord("mic", "Integrity", "CHECKSUM", "DATA_STRING"),
            ]

    class Temperature(Variant):
        def when(self, cells) -> bool:
            return (
                (((cells[1].b1 & 0x60) >> 5) != 0x3)
                & (cells[2].b0 == cells[3].b0)
                & (cells[3].b0 == cells[4].b0)
                & (cells[4].b0 == cells[5].b0)
                & (cells[5].b0 == cells[6].b0)
            )

        def validate_humidity(self, cells, fail_value=DecodeFail.SANITY) -> bool:
            hum = (Rev8(cells[1].b3) >> 4) * 10 + (Rev8(cells[1].b3) & 0xF)
            return hum <= 100

        def sensor_id(self, cells) -> int:
            return Rev8(cells[1].b0)

        def channel(self, cells) -> int:
            return (cells[1].b0 & 0xC) >> 2

        def battery_ok(self, cells) -> int:
            return ((cells[1].b1 & 0x80) >> 7) == 0

        def temperature_C(self, cells) -> float:
            return (
                ((Rev8(cells[1].b1) & 0xF0) | (Rev8(cells[1].b2) << 8)) >> 4
            ) * 0.1

        def humidity(self, cells) -> int:
            return (Rev8(cells[1].b3) >> 4) * 10 + (Rev8(cells[1].b3) & 0xF)

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "AlectoV1-Temperature", "DATA_STRING"),
                JsonRecord("id", "House Code", self.sensor_id, "DATA_INT"),
                JsonRecord("channel", "Channel", self.channel, "DATA_INT"),
                JsonRecord("battery_ok", "Battery", self.battery_ok, "DATA_INT"),
                JsonRecord(
                    "temperature_C",
                    "Temperature",
                    self.temperature_C,
                    "DATA_DOUBLE",
                    fmt="%.2f C",
                ),
                JsonRecord(
                    "humidity", "Humidity", self.humidity, "DATA_INT", fmt="%u %%"
                ),
                JsonRecord("mic", "Integrity", "CHECKSUM", "DATA_STRING"),
            ]


decoders = (alectov1(),)
