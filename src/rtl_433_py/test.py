from rf_sender import RfSender

test = RfSender("Hello!", "MOD_2FSK", 512, 1)
test.send_message()