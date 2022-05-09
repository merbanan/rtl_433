#!/usr/bin/env python3

"""rtl_433 maintainer updates to build files and docs."""

import sys
import os
import subprocess
import glob
import re

def require_clean_work_tree():
    """Check if the working tree is clean, exit otherwise."""
    clean = not len(subprocess.check_output(["git", "diff", "--stat"]))
    if not clean:
        print("Please commit or stash your changes.")
        exit(1)


def grep_lines(pattern, filepath):
    with open(filepath, 'r') as file:
        filedata = file.read()
    regex = re.compile(pattern)
    return regex.findall(filedata)


def replace_text(pattern, repl, filepath):
    with open(filepath, 'r') as file:
        filedata = file.read()
    regex = re.compile(pattern)
    filedata = regex.sub(repl, filedata)
    with open(filepath, 'w') as file:
        file.write(filedata)


def replace_block(from_pattern, to_pattern, repl, filepath):
    with open(filepath, 'r') as file:
        filedata = file.read()
    pattern = '(?ms)(' + from_pattern + ').*?(' + to_pattern + ')'
    repl = r'\g<1>%s\g<2>' % repl
    regex = re.compile(pattern)
    filedata = regex.sub(repl, filedata)
    with open(filepath, 'w') as file:
        file.write(filedata)


def get_help_text(option):
    try:
        help_text = subprocess.check_output(
            ["./build/src/rtl_433", "-c", "0", option], stderr=subprocess.STDOUT).decode('utf-8')
    except subprocess.CalledProcessError as e:
        help_text = e.output.decode('utf-8')

    # trim help text
    help_text = re.sub(r'(?s).*Usage:', '', help_text)
    help_text = re.sub(r'(?s).*option requires an argument -- \'?.\'?', '', help_text)
    # help_text = re.sub(r'(?m)^\s*=\s+(.*)\s+=\s*$', r'### \1', help_text)
    return help_text


def markup_man_text(help_text):
    # sub section headings
    help_text = re.sub(r'(?m)^\s*=\s+(.*)\s+=\s*$', r'.SS "\1"', help_text)
    # indented lines
    help_text = re.sub(r'(?m)^\t(.*)$', r'.RS\n\1\n.RE', help_text)
    # options
    help_text = re.sub(r'(?m)^\s*\[(\S*)(.*)\]\s*(.*)$',
                       r'.TP\n[ \\\\fB\1\\\\fI\2\\\\fP ]\n\3', help_text)
    # fix hyphens
    help_text = re.sub(r'-', '\\-', help_text)
    # fix quotes
    help_text = re.sub(r'(?m)^\'', ' \'', help_text)
    return help_text


def parse_devices(devices_text):
    devices = []
    for line in devices_text.splitlines():
        # match the [123] device number
        device_info = re.search("\[(\d{1,5})\](.) (.*)", line)
        if not device_info:
            continue
        device_number = int(device_info.group(1).strip(), base=10)
        is_disabled = device_info.group(2).strip() == "*"
        device_text = device_info.group(3).strip()

        devices.append((device_number, device_text, is_disabled))
    return devices


verbose = '-v' in sys.argv

# Make sure we run from the top dir
topdir = os.path.dirname(os.path.abspath(__file__))
os.chdir(topdir)

# Only ever run on a clean working tree
require_clean_work_tree()

# glob all src and device files
os.chdir("src")
src_files = sorted(glob.glob('*.c'))
if (verbose):
    print("src_files =", src_files)
device_files = sorted(glob.glob('devices/*.c'))
if (verbose):
    print("device_files =", device_files)
os.chdir("..")

# glob all includes
os.chdir("include")
include_files = sorted(glob.glob('*.h'))
if (verbose):
    print("include_files =", include_files)
os.chdir("..")

# grep all r_devices
r_devices = [grep_lines(r'(?m)^r_device\s*(.*?)\s*=.*',
                        os.path.join("src", p)) for p in device_files]
r_devices = [item for sublist in r_devices for item in sublist]
if (verbose):
    print("r_devices =", r_devices)

# count r_devices, correct for 'new_template' being used six times
r_devices_used = len(r_devices) + 5

# src/CMakeLists.txt
repl = src_files + device_files
repl.remove('rtl_433.c') # exclude apps from lib sources
repl = '\n    ' + ('\n    '.join(repl)) + '\n'
replace_block(r'add_library\(r_433 STATIC$',
              r'^\)', repl, 'src/CMakeLists.txt')

# include/rtl_433.h
# update '#define MAX_PROTOCOLS ?' with actual count
#replace_text(r'(?m)(#define\s+MAX_PROTOCOLS\s+)\d+',
#             r'\g<1>%d' % r_devices_used, 'include/rtl_433.h')

# include/rtl_433_devices.h
# check that everything between '#define DEVICES' and \n\n with DECL(device_name) matches r_devices
# TODO: implement the check...

if (not os.path.isfile("./build/src/rtl_433")):
    print("\nWARNING: rtl_433 binary not found: skipping README/man generation!\n")
    exit(0)

# README.md
# Replace everything between ``` with help output.
repl = '\n' + get_help_text('-h') + '\n'
devices = get_help_text('-R') + '\n'
repl2 = get_help_text('-d') + '\n'
repl2 += get_help_text('-g') + '\n'
repl2 += get_help_text('-X') + '\n'
repl2 += get_help_text('-F') + '\n'
repl2 += get_help_text('-M') + '\n'
repl2 += get_help_text('-r') + '\n'
repl2 += get_help_text('-w') + '\n'
replace_block(r'```',
              r'```', repl + devices + repl2, 'README.md')

# conf/rtl_433.example.conf
parsed_devices = parse_devices(devices)
conf_text = ""
for dev_num, dev_descr, disabled in parsed_devices:
    comment = "# " if disabled else "  "
    spaces = (4 - len(str(dev_num))) * " "
    text = f"{comment}protocol {dev_num}{spaces}# {dev_descr}\n"
    conf_text += text
    #print(dev_num, "-" if disabled else "+", dev_descr)
print(conf_text)
replace_block("## Protocols to enable \(command line option \"-R\"\)\n",
        "## Flex devices", "\n" + conf_text + "\n", "conf/rtl_433.example.conf")

# MAN pages
repl = markup_man_text(repl + repl2)
replace_block(r'\.\\" body',
              r'\.\\" end', '\n'+repl, 'man/man1/rtl_433.1')
