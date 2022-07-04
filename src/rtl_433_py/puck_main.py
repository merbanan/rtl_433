#!/usr/bin/python3.8

from gi.repository import GLib
from puck_bits_receiver import PuckBitsReceiver

puck_bit_receiver = PuckBitsReceiver()
mainloop = GLib.MainLoop()
mainloop.run()



