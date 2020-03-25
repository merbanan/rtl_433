# Compilation instructions for Visual Studio

To compile the program in Visual Studio 2015 (newer versions should work as well) there are external dependancies that are required

Dependancy | Source
---------|----------
 rtl-sdr | You can use the official, but outdated (January 2014)) windows build <https://osmocom.org/attachments/2242/RelWithDebInfo.zip.> Or you can use the Win64 build of a more recent version from <https://github.com/winterrace/librtlsdr> (or follow the compilation instructions).
 libusb | Download prebuilt binaries or build from source <https://libusb.info/>

The folder structure has to look like this

    rtl_433
        vs15
        <and so on>

    rtl-sdr
        x64
        <and so on>
    
    libusb
        MS64
            dll
        <and so on>

Then simply open rtl_433/vs15/rtl_433.sln in Visual Studio and select between debug/release build options.