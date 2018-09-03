Compilation instructions
===========
To compile the program in Visual Studio 2015 (newer versions should work as well), you first need to extract the rtl-sdr library into a folder "rtl-sdr" parallel to the rtl_433_win folder.

You can use the official, but outdated (January 2014)) windows build https://osmocom.org/attachments/2242/RelWithDebInfo.zip 
Or you can use the Win64 build of a more recent version from https://github.com/winterrace/librtlsdr (or follow the compilation instructions).


The folder structure has to look like this

    rtl_433_win
        include
        src
        vs15
        <and so on>
    
    rtl-sdr
        x64
        <and so on>
    
Then simply open rtl_433_win/vs15/rtl_433.sln in Visual Studio and select between debug/release build options.
