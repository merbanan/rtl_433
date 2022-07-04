import time, sys, rflib, struct
from rflib import *
from struct import *

class RfSender:
    """
    This class stores the message, modulation, frequency, and a few other things.
    It also has a method to send the message.
    """
    def __init__(self, message: str, modulation_type:str, baud_rate:int, packet_length:int, frequency=433920000) -> None:
        self.message = message # The message is received as unicode characters
        self.frequency = frequency
        self.modulation_type = modulation_type
        self.baud_rate = baud_rate
        self.packet_length = packet_length
    
    def send_message(self):
        d = RfCat()
        d.setMdmModulation(MOD_2FSK)
        d.setFreq(self.frequency)
        d.makePktFLEN(self.packet_length) # This is the packet length.
        d.setMdmSyncMode(0) # Disable syncword and preamble as this is not used by remote
        d.setMdmDRate(self.baud_rate) # This sets the modulation
        # d.setMaxPower
        d.setModeTX() # This is the transmitter mode

        KEY_DEMOD = int(self.message, 2)
        KEY_PACK = pack(">Q", KEY_DEMOD)
        KEY_LEN = len(KEY_PACK)

        print("Sending Message...")
        try: 
            for i in range(10):
                d.RFxmit(b"HELLO")
            print("Message sent!")
        except:
            print("Error in sending message!")
        d.setModeIDLE()