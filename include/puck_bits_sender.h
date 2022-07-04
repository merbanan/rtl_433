//
// Created by lakhan on 1/7/22.
//

#ifndef RTL433_PUCK_BITS_SENDER_H
#define RTL433_PUCK_BITS_SENDER_H

#include <dbus/dbus.h>

extern const char INTERFACE_NAME[];
extern const char BUS_NAME[];
extern const char OBJECT_PATH_NAME[];
extern const char METHOD_NAME[];
extern const char BITS_SENDER_BUS_NAME[];
extern DBusError err;
extern DBusConnection *DBUS_CONN;
extern int DBUS_RET;

int send_message_through_dbus(char *const msg);

int send_bits_to_receiver(char *const msg);

int init_dbus_connection(void);

#endif // RTL433_PUCK_BITS_SENDER_H
