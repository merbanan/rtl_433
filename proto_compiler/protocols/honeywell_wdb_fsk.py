"""Honeywell ActivLink, wireless door bell, PIR Motion sensor (FSK)."""

from proto_compiler.dsl import Modulation, ModulationConfig
from proto_compiler.protocols.honeywell_wdb_common import honeywell_wdb_base


class honeywell_wdb_fsk(honeywell_wdb_base):
    class modulation_config(ModulationConfig):
        device_name = "Honeywell ActivLink, Wireless Doorbell (FSK)"
        output_model = "Honeywell-ActivLinkFSK"
        modulation = Modulation.FSK_PULSE_PWM
        short_width = 160
        long_width = 320
        gap_limit = 0
        reset_limit = 560
        sync_width = 500
