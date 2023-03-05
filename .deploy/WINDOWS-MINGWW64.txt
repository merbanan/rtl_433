# rtl_433 Windows MinGW-W64 build

The "static" builds are self-contained but do not include SoapySDR support.
The regular builds depend on LibUSB, RTL-SDR, and SoapySDR libraries.

There are no TLS builds (mqtts and influxs) currently.

For the SoapySDR builds you need PothosSDR installed https://downloads.myriadrf.org/builds/PothosSDR/
Any recent version should work, currently built with 2021.07.25-vc16:
https://downloads.myriadrf.org/builds/PothosSDR/PothosSDR-2021.07.25-vc16-x64.exe
When installing choose "Add PothosSDR to the system PATH for the current user"
Remove the SoapySDR.dll in this directory, it's for testing only and won't load any driver modules.

An alternative to installing SoapySDR from PothosSDR is to extract the installer
and copy the builds (.exe) from this release to the `bin` directory in PothosSDR.

## Quick start

To run rtl_433 you have to open Windows Command Prompt (cmd.exe) or Windows Terminal,
then type "cd (rtl_433 folder directory, not rtl_433.exe)"
Example: "cd C:\Users\(name)\Downloads\(rtl_433 folder)"
If typed correctly, the command prompt should change to your rtl_433 folder directory,
then type "rtl_433 (option(s))" and enter, rtl_433 will then start running with the options given.

Running the "-F http" option would look like this:
(directory)> rtl_433 -F http
Press enter and rtl_433 should successfully launch.
When it does, go to http://127.0.0.1:8433/

