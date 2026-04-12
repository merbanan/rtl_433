"""Honeywell ActivLink common protocol definition."""

from proto_compiler.dsl import BitbufferPipeline, Bits, DecodeFail, Protocol


class honeywell_wdb_base(Protocol):
    """Abstract shared payload — concrete subclasses inherit from this AND Decoder."""

    def prepare(self, buf: BitbufferPipeline) -> BitbufferPipeline:
        return buf.find_repeated_row(4, 48).invert()

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

    def validate_packet(self, buf: BitbufferPipeline, fail_value=DecodeFail.SANITY) -> bool: ...

    def subtype(self, class_raw) -> str: ...

    def alert(self, alert_raw) -> str: ...

    def battery_ok(self, battery_low) -> int:
        return battery_low == 0
