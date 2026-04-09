"""Honeywell ActivLink common protocol definition."""

from proto_compiler.dsl import Bits, JsonRecord, ModulationConfig, Protocol


class honeywell_wdb_base(Protocol):
    class modulation_config(ModulationConfig):
        output_model = "Honeywell-ActivLink"

    def prepare(self):
        return self.bitbuffer.find_repeated_row(4, 48).invert()

    device: Bits[20]
    _unused0: Bits[6]
    class_raw: Bits[2]
    _unused1: Bits[10]
    alert_raw: Bits[2]
    _unused2: Bits[3]
    secret_knock: Bits[1]
    relay: Bits[1]
    _unused3: Bits[1]
    battery_low: Bits[1]
    _parity_bit: Bits[1]

    def validate_packet(self, b): ...

    def subtype(self, class_raw) -> str: ...

    def alert(self, alert_raw) -> str: ...

    def battery_ok(self, battery_low) -> int:
        return battery_low == 0

    def to_json(self) -> list[JsonRecord]:
        model = self.__protocol_cls__.modulation_config.output_model
        # fmt: off
        return [
            JsonRecord("model",        "",            model,                "DATA_STRING"),
            JsonRecord("subtype",      "Class",       self.subtype,          "DATA_STRING"),
            JsonRecord("id",           "Id",          self.device,           "DATA_INT", fmt="%x"),
            JsonRecord("battery_ok",   "Battery",     self.battery_ok,       "DATA_INT"),
            JsonRecord("alert",        "Alert",       self.alert,            "DATA_STRING"),
            JsonRecord("secret_knock", "Secret Knock", self.secret_knock,    "DATA_INT", fmt="%d"),
            JsonRecord("relay",        "Relay",       self.relay,            "DATA_INT", fmt="%d"),
            JsonRecord("mic",          "Integrity",   "PARITY",              "DATA_STRING"),
        ]  # fmt: on
