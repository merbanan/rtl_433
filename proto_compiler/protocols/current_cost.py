"""CurrentCost TX, CurrentCost EnviR current sensors.

Two framing variants (EnviR and Classic) with Manchester encoding.
The EnviR transmits a 4-byte preamble (0x55555555) and 2-byte syncword (0x2DD4).
The Classic uses a different init pattern (0xCCCCCCCE915D).

Both variants decode a 4-bit message type and 12-bit device ID, followed
by either three channel power readings (msg_type 0) or a counter/impulse
reading (msg_type 4).
"""

from proto_compiler.dsl import (
    Bits,
    JsonRecord,
    Modulation,
    ModulationConfig,
    Protocol,
    Variant,
)


class current_cost_base(Protocol):
    class modulation_config(ModulationConfig):
        device_name = "CurrentCost Current Sensor"
        modulation = Modulation.FSK_PULSE_PCM
        short_width = 250
        long_width = 250
        reset_limit = 8000

    msg_type: Bits[4]
    device_id: Bits[12]

    class meter(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 0
        ch0_valid: Bits[1]
        ch0_power: Bits[15]
        ch1_valid: Bits[1]
        ch1_power: Bits[15]
        ch2_valid: Bits[1]
        ch2_power: Bits[15]

        def power0_W(self, ch0_valid, ch0_power) -> int:
            return ch0_valid * ch0_power

        def power1_W(self, ch1_valid, ch1_power) -> int:
            return ch1_valid * ch1_power

        def power2_W(self, ch2_valid, ch2_power) -> int:
            return ch2_valid * ch2_power

    class counter(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 4
        _unused: Bits[8]
        sensor_type: Bits[8]
        impulse: Bits[32]


class current_cost_envir(current_cost_base):
    def prepare(self):
        return (
            self.bitbuffer
            .invert()
            .search_preamble(0x55555555A457, bit_length=48)
            .skip_bits(47)
            .manchester_decode()
        )

    class meter(current_cost_base.meter):
        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",    "",          "CurrentCost-EnviR", "DATA_STRING"),
                JsonRecord("id",       "Device Id", self.device_id,      "DATA_INT"),
                JsonRecord("power0_W", "Power 0",   self.power0_W,       "DATA_INT"),
                JsonRecord("power1_W", "Power 1",   self.power1_W,       "DATA_INT"),
                JsonRecord("power2_W", "Power 2",   self.power2_W,       "DATA_INT"),
            ]  # fmt: on

    class counter(current_cost_base.counter):
        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",   "",          "CurrentCost-EnviRCounter", "DATA_STRING"),
                JsonRecord("subtype", "Sensor Id", self.sensor_type,           "DATA_INT"),
                JsonRecord("id",      "Device Id", self.device_id,             "DATA_INT"),
                JsonRecord("power0",  "Counter",   self.impulse,               "DATA_INT"),
            ]  # fmt: on


class current_cost_classic(current_cost_base):
    def prepare(self):
        return (
            self.bitbuffer
            .invert()
            .search_preamble(0xCCCCCCCE915D, bit_length=45)
            .skip_bits(45)
            .manchester_decode()
        )

    class meter(current_cost_base.meter):
        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",    "",          "CurrentCost-TX", "DATA_STRING"),
                JsonRecord("id",       "Device Id", self.device_id,   "DATA_INT"),
                JsonRecord("power0_W", "Power 0",   self.power0_W,    "DATA_INT"),
                JsonRecord("power1_W", "Power 1",   self.power1_W,    "DATA_INT"),
                JsonRecord("power2_W", "Power 2",   self.power2_W,    "DATA_INT"),
            ]  # fmt: on

    class counter(current_cost_base.counter):
        def to_json(self) -> list[JsonRecord]:
            # fmt: off
            return [
                JsonRecord("model",   "",          "CurrentCost-Counter", "DATA_STRING"),
                JsonRecord("subtype", "Sensor Id", self.sensor_type,      "DATA_INT"),
                JsonRecord("id",      "Device Id", self.device_id,        "DATA_INT"),
                JsonRecord("power0",  "Counter",   self.impulse,          "DATA_INT"),
            ]  # fmt: on


class current_cost(Protocol):
    delegates = (current_cost_envir, current_cost_classic)
