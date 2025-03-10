# Contributing guidelines

The rtl_433 project is built on the work of many contributors analyzing,
documenting, and coding device support.
We are happy to accept your contribution of yet another sensor!

Please check if your contribution is following these guidelines
to improve the feedback loop and decrease the burden for the maintainers.

## Adding a new decoder

Decoders for new device protocols are welcome.
You need to know some C and register the decoder with one line, the rest is automatic.

To get started follow these steps to add a new decoder:
- Clone the repo and create a feature branch.  
  E.g. Clone in Github, checkout and then `git checkout -b feat-mydevice`
- Copy some decoder as template, either one that is already close to what you need or `src/devices/new_template.c`.  
  E.g. `cp src/devices/new_template.c src/devices/my_device.c`
- Change the new decoder (at least the `r_device` name and `.disabled = 0`).  
  E.g. change `r_device const new_template =` to `r_device const my_device =`,
  and `new_template_decode` to `my_device_decode`,
  and `.disabled = 3` to `.disabled = 0`
- Edit `include/rtl_433_devices.h`  
  E.g. add `DECL(my_device) \`
- Add your files with Git (no need to commit yet)  
  E.g. `git add src/devices/my_device.c include/rtl_433_devices.h`
- Run `./maintainer_update.py` to add the CMake compile rules
- Compile, add files with Git again  
  E.g. `git add src/CMakeLists.txt`
- Code and test your decoder, try to follow our code style (you can generally use clang-format).
- Run `./maintainer_update.py` again for the readme files.
- Review and commit your changes, push the changes then create a PR.

## Commit messages

Pull-Requests (PR) will be added as squash commit
and the commit message will likely be updated to follow this format.

For general work, e.g. adding or changing decoders
the commit messages should follow a format of

    <verb> [<decoder_model>] <commit_message>

Verb must be one of the following:

- `Add`: for new additions, e.g. device support
- `Fix`: for changes that don't change anything to input/output (security related or bug fixing)
- `Remove`: for changes that remove behaviour (e.g. some old algorithms are cleaned up)
- `Change`: for changes that modify input/output behaviour (e.g. added checksums, preambles)
- `Improve`: for improvements without changes in normal output/behaviour

Don't prefix general work, e.g. adding a decoder should be `Add support for TheDevice`.

Other commit messages should follow the common format of

    <area_of_work>: <verb> <commit_message>

Area of work is optional and may be one of the following:

- `minor`: other small changes that do not warrant a changelog entry
- `build`: for build / build system / ci related work
- `docs`: for documentation related work, both in code and readme/docs folder
- `test`: for test related work
- `deps`: for changes related to (external) dependencies (e.g. soapysdr is updated or mongoose is updated)
- `examples`: for changes related to examples and scripts
- `cosmetics`: for housekeeping work, code style changes

## Supporting Additional Devices and Test Data

Some device protocol decoders are disabled by default. If you have one of the default-disabled devices
then enable all needed device protocols with the `-R` option.
This will likely produce false positives, use with caution.

The first step in decoding new devices is to record the signals using `-S unknown`.
The signals will be stored individually in files named g**NNN**\_**FFF**M\_**RRR**k.cu8 :

| Parameter | Description
|---------|------------
| **NNN** | signal grabbed number
| **FFF** | frequency
| **RRR** | sample rate

This file can be played back with `rtl_433 -r gNNN_FFFM_RRRk.cu8`.

These files are vital for understanding the signal format as well as the message data.  Use the analyzer
with `-A` to look at the recorded signal and determine the pulse characteristics, e.g. `rtl_433 -r gNNN_FFFM_RRRk.cu8 -A`.

Make sure you have recorded a proper set of test signals representing different conditions together
with any and all information about the values that the signal should represent. For example, make a
note of what temperature and/or humidity is the signal encoding. Ideally, capture a range of data
values, such a different temperatures, to make it easy to spot what part of the message is changing.

Add the data files, a text file describing the captured signals, pictures of the device and/or
a link the manufacturer's page (ideally with specifications) to the rtl_433_tests
github repository. Follow the existing structure as best as possible and send a pull request.

https://github.com/merbanan/rtl_433_tests

Please don't open a new github issue for device support or request decoding help from others
until you've added test signals and the description to the repository.

The rtl_433_test repository is also used to help test that changes to rtl_433 haven't caused any regressions.

## Code style

Indentation is 4 spaces. Check with `clang-format`.

Indent `data_make()` nicely tabular and surround it with
```
/* clang-format off */
...
/* clang-format on */
```

Start your file with a copyright note (indent 4 spaces) like:
```
/** @file
    Bresser Weather Center 5-in-1.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
```

Name your decode function `foobar_decode`.
Put documentation (markdown, no indent) before the decode function:
```
/**
Bresser Weather Center 7-in-1, outdoor sensor.

...supported models etc.

Data layout:

    IIII F TTT HH CC

- I: 16 bit ID
- F: 4 bit flags
- T: 12 bit temperature, scale 10
- H: 8 bit humidity
- C: 8 bit CRC-8, poly 0x81

Format string:

    ID:16h FLAGS:4h TEMP:12h HUMI:8h CRC:8h

...Decoding notes like endianness, signedness
*/
```

The keys in `data_make()` need to contain
- `"model"`, short unique key for this decoder
- `"id", `a unique sensor ID
- `"mic"`, if applicable the integrity check, e.g. `"PARITY"`, `"SUM"`, `"CRC"`, or `"DIGEST"`.

See [JSON Data fields](DATA_FORMAT.md) for common keys.
