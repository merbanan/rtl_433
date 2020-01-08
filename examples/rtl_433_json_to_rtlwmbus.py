#!/usr/bin/env python
""" 
$ rtl_433 -R 104 -F json | rtl_433_json_to_rtlwmbus.py

A script to convert rtl_433 wmbus json output to rtlwmbus output

Copyright (C) 2019 Benjamin Larsson
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

import sys
import json
import time;

def sanitize(text):
    return text.replace(" ", "_")

def rtl_433_wmbus():
    dup = {}
    seconds = 10
    while True:
        line = sys.stdin.readline()
        ts = int(time.time())
        if not line:
            break

        try:
            event = json.loads(line)

            # Duplicate check + check if dictianary is initialized
            id = int(event['id'])
            if id in dup:
                #print("if %s D:%s T:%s" % (id, dup[id], ts))
                if (dup[id] + seconds) < ts:
                    duplicate = False
                    dup[id] = ts;
                else:
                    duplicate = True
                    #print("Dup! %s" % (id))
            else:
                #print("else %s" % (id))
                dup[id] = ts;
                duplicate = False

            if duplicate != True:
                print("%s1;1;1;%s.000;54;46;%s;0x%s" % (event['mode'], event['time'], event['id'], event['data']) )
                sys.stdout.flush()

        except KeyError:
            pass

        except ValueError:
            pass


if __name__ == "__main__":
    dup_test = {}
    rtl_433_wmbus()
