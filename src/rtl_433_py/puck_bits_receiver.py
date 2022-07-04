#
#    Creates a dbus service, used to read rf bits sent from rtl_433
#     
#    Copyright (C) 2022 Alfred Daimari 
#    
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
# 

# TODO - write a print function for all the classes


import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop

DBusGMainLoop(set_as_default=True)
OPATH = "/org/autosec/PuckBitsReceiver"
IFACE = "org.autosec.PuckBitsReceiverInterface"
BUS_NAME = "org.autosec.PuckBitsReceiver"


class BitPack:
    """
    data structure to hold a row of bits
    """

    def __init__(self, bit_string: str, dist_to_prev_bitpk: int) -> None:
        self.dist_to_prev_bitpk = dist_to_prev_bitpk
        self.bit_pk = []
        for bit in bit_string:
            self.bit_pk.append(int(bit))

    def __len__(self) -> int:
        return len(self.bit_pk)


class KeyFobPacket:
    """ 
    data structure to hold a key fob packet \n
    Packet status info: \n
    0 - packet still not fully formed, still accepting more bits \n
    1 - packet fully formed, not accepting any more bits \n
    """

    def __init__(self, bit_string: str, dist_to_prev_bitpk: int) -> None:
        self.packets = [BitPack(bit_string, dist_to_prev_bitpk)]
        self.packet_status = 0
        self.__set_packet_validity()

    def __get_dist_to_prev_pkt(self) -> int:
        """
        get key fob packet distance to previous key fob packet
        """
        return self.packets[0].dist_to_prev_bitpk

    def __get_packet_validity(self) -> bool:
        return self.packet_status == 1

    def __concatenate(self, key_fb_pkt: object) -> bool:
        """
        validate and concatenate to key fob packets together
        """
        if key_fb_pkt.__get_dist_to_prev_pkt() < 120:
            self.packets.append(key_fb_pkt.packets[0])
            return True
        else:
            return False

    def __set_packet_validity(self) -> None:
        """
        check if key fob packet is fully formed or not
        """
        total_bits = 0
        for pk in self.packets:
            total_bits += len(pk)
        if not total_bits < 224:
            self.packet_status = 1  # packet is fully formed, should not allow appending of more bits

    def concatenate(self, key_fb_pkt: object) -> bool:
        """ concatenates two packets if the gap between them is 'very less' """
        if not self.__get_packet_validity():
            concat_ = self.__concatenate(key_fb_pkt)
            self.__set_packet_validity()
            return concat_
        else:
            return False

    def get_complete(self):
        return self.__get_packet_validity()


class RollingKeyFobs:
    """"
    data structure to hold the rolling key fobs,
    send one out, when the length of data structure is 2
    """

    def __init__(self) -> None:
        self.key_fobs_list = []

    def __len__(self):
        return len(self.key_fobs_list)

    def get_can_be_sent(self) -> bool:
        """
        checks if there are two valid key fobs \n
        and if the previous one can be sent or not
        """
        if len(self) > 1:
            if self.key_fobs_list[1].complete:
                return True
            else:
                self.__shift()
                return self.get_can_be_sent()
        else:
            return False

    def __shift(self):
        """
        remove the first element from key fob
        """
        return self.key_fobs_list.pop(0)

    def push(self, bit_string: str, dist_to_prev_bitpk: int):
        """
        add a new key fob to list or concatenate with previous key fob
        """
        tmp_keyfob_pkt = KeyFobPacket(bit_string, dist_to_prev_bitpk)
        lst_key_fob = self.key_fobs_list[-1]
        apnd_bool = lst_key_fob.concatenate(tmp_keyfob_pkt)

        if not apnd_bool:
            self.key_fobs_list.append(tmp_keyfob_pkt)

        if self.can_be_sent:
            keyfob_tb_snt = self.__shift()
            # send keyfob
            del keyfob_tb_snt


class PuckBitsReceiver(dbus.service.Object):
    """ For creating a rf bits listener service on dbus"""

    def __init__(self) -> None:
        bus = dbus.SystemBus()
        bus.request_name(BUS_NAME)
        bus_name = dbus.service.BusName(BUS_NAME, bus=bus)
        dbus.service.Object.__init__(self, bus_name, OPATH)

    @dbus.service.method(dbus_interface=IFACE, in_signature="s", out_signature="s", sender_keyword="sender",
                         connection_keyword="conn")
    def ReceiveBits(self, bits, sender=None, conn=None):
        """
        receiver function for bits
        """
        #
        #
        # perform bit operations
        #
        #
        print(bits)
        return bits
