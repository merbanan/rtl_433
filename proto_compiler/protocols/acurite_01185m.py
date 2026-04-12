"""Decoder for Acurite Grill/Meat Thermometer 01185M.

Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
Based on work by Joe "exeljb"

Modulation:

- 56 bit PWM data
- short is 840 us pulse, 2028 us gap
- long is 2070 us pulse, 800 us gap,
- sync is 6600 us pulse, 4080 gap,
- there is no packet gap and 8 repeats
- data is inverted (short=0, long=1) and byte-reflected

S.a. #1824

Temperature is 16 bit, degrees F, scaled x10 +900.
The first reading is the "Meat" channel and the second is for the "Ambient" or
grill temperature.

- A value of 0x1b58 (7000 / 610F) indicates the sensor is unplugged (E1).
- A value of 0x00c8 (200 / -70F) indicates a sensor problem (E2).

Each probe is independently plugged-in/unplugged.  The decoder emits one of
four variants depending on which probes report valid readings, so that
temperature fields are present only when the corresponding probe is live.

The battery status is the MSB of the second byte, 0 for good battery, 1 for low.

Channel appears random (often 3, 6, 12, 15).

Data layout (56 bits):

    II BC MM MM TT TT XX

- I: 8 bit ID
- B: 1 bit battery-low (MSB of second byte); middle bits skipped below
- C: 4 bit channel (low nibble of second byte)
- M: 16 bit temperature 1 (F x10 +900)
- T: 16 bit temperature 2
- X: 8 bit checksum, add-with-carry over first 6 bytes
"""

from proto_compiler.dsl import (
    BitbufferPipeline,
    Bits,
    DecodeFail,
    Decoder,
    FirstValid,
    JsonRecord,
    Modulation,
    ModulationConfig,
    Repeatable,
    Rows,
    Variant,
)


class acurite_01185m_row(Repeatable):
    id: Bits[8]
    battery_low: Bits[1]
    mid: Bits[3]
    channel: Bits[4]
    temp1_raw: Bits[16]
    temp2_raw: Bits[16]
    checksum: Bits[8]


# A probe raw value is valid iff it's in (200, 7000).
# 0x00c8 (200, -70F) = sensor problem, 0x1b58 (7000, 610F) = unplugged.
_TEMP_LO = 200
_TEMP_HI = 7000


class _AcuriteProbeVariant(Variant):
    """Shared methods for acurite_01185m variants.

    This base is never dispatched (its ``when`` is unreachable); concrete
    subclasses below override ``when`` and ``to_json``.
    """

    def when(self) -> bool:
        return False

    def temp1_f(self, data_temp1_raw) -> float:
        return (data_temp1_raw - 900) * 0.1

    def temp2_f(self, data_temp2_raw) -> float:
        return (data_temp2_raw - 900) * 0.1

    def battery_ok(self, data_battery_low) -> int:
        return data_battery_low == 0


class acurite_01185m(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Acurite Grill/Meat Thermometer 01185M",
            modulation=Modulation.OOK_PULSE_PWM,
            short_width=840,
            long_width=2070,
            sync_width=6600,
            gap_limit=3000,
            reset_limit=6000,
        )

    def prepare(self, buf: BitbufferPipeline) -> BitbufferPipeline:
        return buf.invert().reflect()

    data: Rows[FirstValid(56), acurite_01185m_row]

    def validate_checksum(self, data_id, data_battery_low, data_mid, data_channel, data_temp1_raw, data_temp2_raw, data_checksum, fail_value=DecodeFail.MIC) -> bool: ...

    class BothProbes(_AcuriteProbeVariant):
        """Both probes report valid temperatures."""

        def when(self, data_temp1_raw, data_temp2_raw) -> bool:
            return (
                (data_temp1_raw > _TEMP_LO) & (data_temp1_raw < _TEMP_HI)
                & (data_temp2_raw > _TEMP_LO) & (data_temp2_raw < _TEMP_HI)
            )

        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",           "",          "Acurite-01185M",    "DATA_STRING"),
                JsonRecord("id",              "",          self.data_id,        "DATA_INT"),
                JsonRecord("channel",         "",          self.data_channel,   "DATA_INT"),
                JsonRecord("battery_ok",      "Battery",   self.battery_ok,     "DATA_INT"),
                JsonRecord("temperature_1_F", "Meat",      self.temp1_f,        "DATA_DOUBLE", fmt="%.1f F"),
                JsonRecord("temperature_2_F", "Ambient",   self.temp2_f,        "DATA_DOUBLE", fmt="%.1f F"),
                JsonRecord("mic",             "Integrity", "CHECKSUM",          "DATA_STRING"),
            ]
            # fmt: on

    class MeatOnly(_AcuriteProbeVariant):
        """Only the meat probe (probe 1) is plugged in."""

        def when(self, data_temp1_raw, data_temp2_raw) -> bool:
            return (
                (data_temp1_raw > _TEMP_LO) & (data_temp1_raw < _TEMP_HI)
                & ((data_temp2_raw <= _TEMP_LO) | (data_temp2_raw >= _TEMP_HI))
            )

        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",           "",          "Acurite-01185M",    "DATA_STRING"),
                JsonRecord("id",              "",          self.data_id,        "DATA_INT"),
                JsonRecord("channel",         "",          self.data_channel,   "DATA_INT"),
                JsonRecord("battery_ok",      "Battery",   self.battery_ok,     "DATA_INT"),
                JsonRecord("temperature_1_F", "Meat",      self.temp1_f,        "DATA_DOUBLE", fmt="%.1f F"),
                JsonRecord("mic",             "Integrity", "CHECKSUM",          "DATA_STRING"),
            ]
            # fmt: on

    class AmbientOnly(_AcuriteProbeVariant):
        """Only the ambient/grill probe (probe 2) is plugged in."""

        def when(self, data_temp1_raw, data_temp2_raw) -> bool:
            return (
                ((data_temp1_raw <= _TEMP_LO) | (data_temp1_raw >= _TEMP_HI))
                & (data_temp2_raw > _TEMP_LO) & (data_temp2_raw < _TEMP_HI)
            )

        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",           "",          "Acurite-01185M",    "DATA_STRING"),
                JsonRecord("id",              "",          self.data_id,        "DATA_INT"),
                JsonRecord("channel",         "",          self.data_channel,   "DATA_INT"),
                JsonRecord("battery_ok",      "Battery",   self.battery_ok,     "DATA_INT"),
                JsonRecord("temperature_2_F", "Ambient",   self.temp2_f,        "DATA_DOUBLE", fmt="%.1f F"),
                JsonRecord("mic",             "Integrity", "CHECKSUM",          "DATA_STRING"),
            ]
            # fmt: on

    class NoProbes(_AcuriteProbeVariant):
        """Neither probe reports a valid temperature."""

        def when(self, data_temp1_raw, data_temp2_raw) -> bool:
            return (
                ((data_temp1_raw <= _TEMP_LO) | (data_temp1_raw >= _TEMP_HI))
                & ((data_temp2_raw <= _TEMP_LO) | (data_temp2_raw >= _TEMP_HI))
            )

        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",      "",          "Acurite-01185M",  "DATA_STRING"),
                JsonRecord("id",         "",          self.data_id,      "DATA_INT"),
                JsonRecord("channel",    "",          self.data_channel, "DATA_INT"),
                JsonRecord("battery_ok", "Battery",   self.battery_ok,   "DATA_INT"),
                JsonRecord("mic",        "Integrity", "CHECKSUM",        "DATA_STRING"),
            ]
            # fmt: on


decoders = (acurite_01185m(),)
