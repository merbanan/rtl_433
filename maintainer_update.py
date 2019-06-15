#!/usr/bin/env python

"""rtl_433 maintainer updates to build files and docs."""

from __future__ import print_function
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
            ["./build/src/rtl_433", "-c", "0", option], stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        help_text = e.output

    # trim help text
    help_text = re.sub(r'(?s).*Usage:', 'Usage:', help_text)
    help_text = re.sub(r'(?s).*option requires an argument -- .',
                       'Option ' + option + ':', help_text)
    return help_text


# Make sure we run from the top dir
topdir = os.path.dirname(os.path.abspath(__file__))
os.chdir(topdir)

# Only ever run on a clean working tree
require_clean_work_tree()

# glob all src and device files
os.chdir("src")
src_files = sorted(glob.glob('*.c'))
print("src_files =", src_files)
device_files = sorted(glob.glob('devices/*.c'))
print("device_files =", device_files)
os.chdir("..")

# glob all includes
os.chdir("include")
include_files = sorted(glob.glob('*.h'))
print("include_files =", include_files)
os.chdir("..")

# grep all r_devices
r_devices = [grep_lines(r'(?m)^r_device\s*(.*?)\s*=.*',
                        os.path.join("src", p)) for p in device_files]
r_devices = [item for sublist in r_devices for item in sublist]
print("r_devices =", r_devices)

# count r_devices, correct for 'template' being used six times
r_devices_used = len(r_devices) + 5

# README.md
# Replace everything between ``` with help output.
repl = '\n' + get_help_text('-h') + '\n'
repl += get_help_text('-R') + '\n'
repl += get_help_text('-d') + '\n'
repl += get_help_text('-g') + '\n'
repl += get_help_text('-X') + '\n'
repl += get_help_text('-F') + '\n'
repl += get_help_text('-M') + '\n'
repl += get_help_text('-r') + '\n'
repl += get_help_text('-w') + '\n'
replace_block(r'```',
              r'```', repl, 'README.md')

# src/CMakeLists.txt
repl = src_files + device_files
repl = '\n\t' + ('\n\t'.join(repl)) + '\n'
replace_block(r'add_executable\(rtl_433$',
              r'^\)', repl, 'src/CMakeLists.txt')

# src/Makefile.am
# replace everything between 'rtl_433_SOURCES = ' and \n\n with *.c and devices/*.c from src
repl = src_files + device_files
repl = (' \\\n                       '.join(repl)) + '\n'
replace_block(r'rtl_433_SOURCES      = ',
              r'^$', repl, 'src/Makefile.am')

# include/rtl_433.h
# update '#define MAX_PROTOCOLS ?' with actual count
#replace_text(r'(?m)(#define\s+MAX_PROTOCOLS\s+)\d+',
#             r'\g<1>%d' % r_devices_used, 'include/rtl_433.h')

# include/rtl_433_devices.h
# check that everything between '#define DEVICES' and \n\n with DECL(device_name) matches r_devices
# TODO: implement the check...

# vs15/rtl_433.vcxproj
#  <ItemGroup>
#    <ClInclude Include="..\include\baseband.h" />
#    ...
#  </ItemGroup>
repl = (r'" />\n    <ClInclude Include="..\\include\\'.join(include_files))
replace_block(r'^  <ItemGroup>\n    <ClInclude Include="..\\include\\',
              r'" />\n    <ClInclude Include="..\\src\\getopt', repl, 'vs15/rtl_433.vcxproj')
#  <ItemGroup>
#    <ClCompile Include="..\src\baseband.c" />
#    ...
#  </ItemGroup>
repl = src_files + device_files
repl = [p.replace('/', r'\\') for p in repl]
repl = (r'" />\n    <ClCompile Include="..\\src\\'.join(repl))
replace_block(r'^  <ItemGroup>\n    <ClCompile Include="..\\src\\',
              r'" />\n    <ClCompile Include="..\\src\\getopt', repl, 'vs15/rtl_433.vcxproj')

# vs15/rtl_433.vcxproj.filters
#  <ItemGroup>
#    <ClInclude Include="..\include\baseband.h">
#      <Filter>Header Files</Filter>
#    </ClInclude>
#    ...
#  </ItemGroup>
# "Header Files"
repl = (r'">\n      <Filter>Header Files</Filter>\n    </ClInclude>\n    <ClInclude Include="..\\include\\'.join(include_files))
replace_block(r'^  <ItemGroup>\n    <ClInclude Include="..\\include\\',
              r'">\n      <Filter>Header Files</Filter>\n    </ClInclude>\n    <ClInclude Include="..\\src\\getopt', repl, 'vs15/rtl_433.vcxproj.filters')
# "Source Files"
repl = (r'">\n      <Filter>Source Files</Filter>\n    </ClCompile>\n    <ClCompile Include="..\\src\\'.join(src_files))
replace_block(r'^  <ItemGroup>\n    <ClCompile Include="..\\src\\',
              r'">\n      <Filter>Source Files</Filter>\n    </ClCompile>\n    <ClCompile Include="..\\src\\getopt', repl, 'vs15/rtl_433.vcxproj.filters')
# "Source Files\devices"
repl = [p.replace('devices/', '') for p in device_files]
repl = (r'">\n      <Filter>Source Files\\devices</Filter>\n    </ClCompile>\n    <ClCompile Include="..\\src\\devices\\'.join(repl))
replace_block(r'^    <ClCompile Include="..\\src\\devices\\',
              r'">\n      <Filter>Source Files\\devices</Filter>\n    </ClCompile>\n  </ItemGroup>', repl, 'vs15/rtl_433.vcxproj.filters')
