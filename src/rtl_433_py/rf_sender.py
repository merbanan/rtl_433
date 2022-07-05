from ast import Constant
import time, sys
from rflib import *
from struct import *

class RfSender:
    """
    This class stores the message, modulation, frequency, and a few other things.
    It also has a method to send the message.
    """
    def __init__(self, packet_length:int, baud_rate:int, frequency=433920000, modulation_type=MOD_ASK_OOK) -> None:
        self.frequency = frequency
        self.modulation_type = modulation_type
        self.baud_rate = baud_rate
        self.packet_length = packet_length
    
    def send_message(self, message):
        d = RfCat()
        d.setMdmModulation(self.modulation_type)
        d.setFreq(self.frequency)
        d.makePktFLEN(self.packet_length) # This is the packet length.
        d.setMdmSyncMode(0) # Disable syncword and preamble as this is not used by remote
        d.setMdmDRate(self.baud_rate) # This sets the modulation
        # d.setMaxPower
        d.setModeTX() # This is the transmitter mode

        KEY_DEMOD = int(message, 2)
        KEY_PACK = pack(">Q", KEY_DEMOD)
        KEY_LEN = len(KEY_PACK)

        print("Sending Message...")
        try: 
            d.RFxmit(KEY_PACK)
            print("Message sent!")
        except:
            print("Error in sending message!")
        d.setModeIDLE()