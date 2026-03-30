"""Honeywell ActivLink, wireless door bell, PIR Motion sensor (OOK)."""

from proto_compiler.dsl import Modulation
from proto_compiler.protocols.honeywell_wdb_common import honeywell_wdb_base


class honeywell_wdb(honeywell_wdb_base):
    class config(honeywell_wdb_base.config):
        device_name = "Honeywell ActivLink, Wireless Doorbell"
        modulation = Modulation.OOK_PULSE_PWM
        short_width = 175
        long_width = 340
        gap_limit = 0
        reset_limit = 5000
        sync_width = 500
