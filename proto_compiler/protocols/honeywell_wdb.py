"""Honeywell ActivLink, wireless door bell, PIR Motion sensor (OOK)."""

from proto_compiler.dsl import Decoder, JsonRecord, Modulation, ModulationConfig
from proto_compiler.protocols.honeywell_wdb_common import honeywell_wdb_base


class honeywell_wdb(honeywell_wdb_base, Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Honeywell ActivLink, Wireless Doorbell",
            modulation=Modulation.OOK_PULSE_PWM,
            short_width=175,
            long_width=340,
            gap_limit=0,
            reset_limit=5000,
            sync_width=500,
        )

    def to_json(self) -> list[JsonRecord]:
        # fmt: off
        return [
            JsonRecord("model",        "",             "Honeywell-ActivLink", "DATA_STRING"),
            JsonRecord("subtype",      "Class",        self.subtype,          "DATA_STRING"),
            JsonRecord("id",           "Id",           self.device,           "DATA_INT", fmt="%x"),
            JsonRecord("battery_ok",   "Battery",      self.battery_ok,       "DATA_INT"),
            JsonRecord("alert",        "Alert",        self.alert,            "DATA_STRING"),
            JsonRecord("secret_knock", "Secret Knock", self.secret_knock,     "DATA_INT", fmt="%d"),
            JsonRecord("relay",        "Relay",        self.relay,            "DATA_INT", fmt="%d"),
            JsonRecord("mic",          "Integrity",    "PARITY",              "DATA_STRING"),
        ]  # fmt: on


decoders = (honeywell_wdb(),)
